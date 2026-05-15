#include "LoopAnalysis.hpp"

#include <climits>

#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"

using namespace llvm;

int64_t getOffsetFromGEP(const GEPOperator * GEP, ScalarEvolution * SE)
{
  int64_t off = 0;

  for (auto Idx = GEP->idx_begin(); Idx != GEP->idx_end(); ++Idx)
  {
    const Value * IdxVal = Idx->get();

    if (auto * CI = dyn_cast<ConstantInt>(IdxVal))
    {
      off += CI->getSExtValue();
    }
    else if (SE)
    {
      const SCEV * IdxSCEV = SE->getSCEV(const_cast<Value *>(IdxVal));

      if (const SCEVConstant * C = dyn_cast<SCEVConstant>(IdxSCEV))
      {
        off += C->getAPInt().getSExtValue();
      }
      else if (const SCEVCastExpr * Cast = dyn_cast<SCEVCastExpr>(IdxSCEV))
      {
        const SCEV * Inner = Cast->getOperand();
        if (const SCEVAddRecExpr * AR = dyn_cast<SCEVAddRecExpr>(Inner))
        {
          if (const SCEVConstant * S = dyn_cast<SCEVConstant>(AR->getStart()))
            off += S->getAPInt().getSExtValue();
        }
        else if (const SCEVAddExpr * Add = dyn_cast<SCEVAddExpr>(Inner))
        {
          for (unsigned i = 0; i < Add->getNumOperands(); ++i)
            if (const SCEVConstant * C2 =
                  dyn_cast<SCEVConstant>(Add->getOperand(i)))
              off += C2->getAPInt().getSExtValue();
        }
        else if (const SCEVConstant * C2 = dyn_cast<SCEVConstant>(Inner))
        {
          off += C2->getAPInt().getSExtValue();
        }
      }
      else if (const SCEVAddRecExpr * AR = dyn_cast<SCEVAddRecExpr>(IdxSCEV))
      {
        if (const SCEVConstant * S = dyn_cast<SCEVConstant>(AR->getStart()))
          off += S->getAPInt().getSExtValue();
      }
      else if (const SCEVAddExpr * Add = dyn_cast<SCEVAddExpr>(IdxSCEV))
      {
        for (unsigned i = 0; i < Add->getNumOperands(); ++i)
          if (const SCEVConstant * C2 =
                dyn_cast<SCEVConstant>(Add->getOperand(i)))
            off += C2->getAPInt().getSExtValue();
      }
    }
    else
    {
      off += (int64_t)std::hash<const Value *>()(IdxVal) % 1000000;
    }
  }
  return off;
}

PtrKey normalizePtr(const Value * P, ScalarEvolution * SE)
{
  if (!P) return {nullptr, 0};

  const Value * S = P->stripPointerCasts();
  int64_t Off = 0;

  if (auto * GEP = dyn_cast<GEPOperator>(S))
  {
    const Value * Base0 = GEP->getPointerOperand()->stripPointerCasts();
    Off = getOffsetFromGEP(GEP, SE);
    return {Base0, Off};
  }
  return {S, Off};
}

unsigned long long extractTripCount(BasicBlock * Header)
{
  auto * BI = dyn_cast<BranchInst>(Header->getTerminator());
  if (!BI || !BI->isConditional()) return 0;

  auto * Cmp = dyn_cast<ICmpInst>(BI->getCondition());
  if (!Cmp) return 0;

  ConstantInt * Limit = nullptr;
  if (auto * C = dyn_cast<ConstantInt>(Cmp->getOperand(1)))
    Limit = C;
  else if (auto * C = dyn_cast<ConstantInt>(Cmp->getOperand(0)))
    Limit = C;

  if (!Limit) return 0;

  unsigned long long N = Limit->getZExtValue();
  switch (Cmp->getPredicate())
  {
    case ICmpInst::ICMP_SLT:
    case ICmpInst::ICMP_ULT:
      return N;
    case ICmpInst::ICMP_SLE:
    case ICmpInst::ICMP_ULE:
      return N + 1;
    case ICmpInst::ICMP_EQ:
    case ICmpInst::ICMP_NE:
    case ICmpInst::ICMP_SGT:
    case ICmpInst::ICMP_UGT:
      return N;
    case ICmpInst::ICMP_SGE:
    case ICmpInst::ICMP_UGE:
      return N + 1;
    default:
      return 0;
  }
}

void collectLoopBlocks(
  BasicBlock * Header, std::set<BasicBlock *> & LoopBlocks,
  const std::unordered_map<BasicBlock *, int> & BlockOrder)
{
  std::set<BasicBlock *> Visited;
  std::vector<BasicBlock *> Worklist;
  Worklist.push_back(Header);
  LoopBlocks.insert(Header);

  int HeaderOrder = BlockOrder.at(Header);

  BasicBlock * Latch = nullptr;
  for (auto * Pred : predecessors(Header))
  {
    if (BlockOrder.at(Pred) > HeaderOrder)
    {
      Latch = Pred;
      break;
    }
  }

  while (!Worklist.empty())
  {
    BasicBlock * BB = Worklist.back();
    Worklist.pop_back();

    if (Visited.count(BB)) continue;
    Visited.insert(BB);

    for (BasicBlock * Succ : successors(BB))
    {
      if (Succ == Header) continue;

      int SuccOrder = BlockOrder.at(Succ);
      int LatchOrder = Latch ? BlockOrder.at(Latch) : INT_MAX;

      if (SuccOrder > HeaderOrder && SuccOrder <= LatchOrder &&
          !LoopBlocks.count(Succ))
      {
        LoopBlocks.insert(Succ);
        Worklist.push_back(Succ);
      }
    }
  }
}

std::vector<MemAccessInLoop>
buildIterationSequence(const std::set<BasicBlock *> & LoopBlocks,
                       ScalarEvolution * SE)
{
  std::vector<MemAccessInLoop> Sequence;
  unsigned Position = 0;

  for (BasicBlock * BB : LoopBlocks)
  {
    for (Instruction & I : *BB)
    {
      const Value * Ptr = nullptr;

      if (auto * LI = dyn_cast<LoadInst>(&I))
        Ptr = LI->getPointerOperand();
      else if (auto * SI = dyn_cast<StoreInst>(&I))
        Ptr = SI->getPointerOperand();
      else if (auto * MI = dyn_cast<MemIntrinsic>(&I))
      {
        if (auto * MT = dyn_cast<MemTransferInst>(MI))
          Ptr = MT->getDest();
        else if (auto * MS = dyn_cast<MemSetInst>(MI))
          Ptr = MS->getDest();
      }

      if (!Ptr) continue;

      PtrKey Key = normalizePtr(Ptr, SE);
      if (Key.Base && isa<AllocaInst>(Key.Base)) continue;

      Sequence.push_back({&I, Key, 0, 0, Position++, I.getDebugLoc()});
    }
  }

  return Sequence;
}