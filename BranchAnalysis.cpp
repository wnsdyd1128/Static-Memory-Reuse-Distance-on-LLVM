#include "BranchAnalysis.hpp"

#include "LoopAnalysis.hpp"

#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"

using namespace llvm;

bool isMergeBlock(BasicBlock * BB, BasicBlock * BranchBlock)
{
  int predCount = 0;
  for (auto * Pred : predecessors(BB))
  {
    predCount++;
    if (predCount > 1) return true;
  }
  return false;
}

void collectLeafBranches(
  BasicBlock * BB,
  std::unordered_map<PtrKey, unsigned long long, PtrKeyHash> LastAccessNo,
  unsigned long long MemAccessNo, std::vector<BasicBlock *> & LeafBlocks,
  std::set<BasicBlock *> & Visited)
{
  if (Visited.count(BB)) return;
  Visited.insert(BB);

  int memAccessCount = 0;
  bool hasConditionalBranch = false;

  for (auto & I : *BB)
  {
    if (isa<LoadInst>(&I) || isa<StoreInst>(&I) || isa<MemIntrinsic>(&I))
      memAccessCount++;
    if (auto * BI = dyn_cast<BranchInst>(&I))
      if (BI->isConditional())
        hasConditionalBranch = true;
  }

  if (hasConditionalBranch && memAccessCount <= 1)
  {
    for (BasicBlock * Succ : successors(BB))
      collectLeafBranches(Succ, LastAccessNo, MemAccessNo, LeafBlocks, Visited);
  }
  else
  {
    LeafBlocks.push_back(BB);
  }
}

void measureBranchPathRD(
  BasicBlock * BB,
  std::unordered_map<PtrKey, unsigned long long, PtrKeyHash> & LastAccessNo,
  unsigned long long & MemAccessNo,
  std::unordered_map<PtrKey, std::vector<unsigned long long>, PtrKeyHash> &
    AllRDs,
  std::set<BasicBlock *> & Visited, BasicBlock * StartBlock,
  ScalarEvolution * SE)
{
  if (Visited.count(BB)) return;

  if (BB != StartBlock)
  {
    int predCount = 0;
    for (auto * Pred : predecessors(BB))
    {
      predCount++;
      if (predCount > 1) return;
    }
  }

  Visited.insert(BB);

  for (auto & I : *BB)
  {
    const Value * Ptr = nullptr;
    if (auto * L = dyn_cast<LoadInst>(&I))
      Ptr = L->getPointerOperand();
    else if (auto * S = dyn_cast<StoreInst>(&I))
      Ptr = S->getPointerOperand();
    else if (auto * MI = dyn_cast<MemIntrinsic>(&I))
    {
      if (auto * MT = dyn_cast<MemTransferInst>(MI))
        Ptr = MT->getDest();
      else if (auto * MS = dyn_cast<MemSetInst>(MI))
        Ptr = MS->getDest();
    }

    if (!Ptr) continue;

    unsigned long long CurNo = MemAccessNo++;
    PtrKey Key = normalizePtr(Ptr, SE);

    auto It = LastAccessNo.find(Key);
    if (It != LastAccessNo.end())
    {
      unsigned long long RD =
        (CurNo > It->second) ? (CurNo - It->second) : 0ULL;
      AllRDs[Key].push_back(RD);
    }

    LastAccessNo[Key] = CurNo;
  }

  for (BasicBlock * Succ : successors(BB))
    measureBranchPathRD(Succ, LastAccessNo, MemAccessNo, AllRDs, Visited,
                        StartBlock, SE);
}