#include "BranchAnalysis.hpp"
#include "LoopAnalysis.hpp"
#include "ReuseTypes.hpp"

#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/raw_ostream.h"

#include <set>
#include <unordered_map>

using namespace llvm;

namespace
{

// 재사용 패턴 출력 (기존 intra-iter/loop-carried 포맷 — 루프 섹션 교체 전까지 유지)
static void printLoopRD(StringRef FuncName, const PtrKey & Key,
                        unsigned long long RD, const char * Type,
                        unsigned Occurrences, unsigned long long TripCount,
                        DILocation * Loc)
{
  if (Loc)
  {
    errs() << FuncName << ":" << Loc->getFilename() << ":" << Loc->getLine();
  }
  else
  {
    errs() << FuncName << ":<ir>";
  }

  errs() << "  RD=" << RD << "  type=" << Type
         << "  occurrences=" << Occurrences << "  trip_count=" << TripCount
         << "  base=" << Key.Base << "  off=" << Key.Offset << "\n";
}


struct ReusePass : public PassInfoMixin<ReusePass>
{
  PreservedAnalyses run(Function & F, FunctionAnalysisManager & AM)
  {
    // ★ ScalarEvolution 분석 가져오기
    ScalarEvolution * SE = &AM.getResult<ScalarEvolutionAnalysis>(F);

    std::unordered_map<PtrKey, unsigned long long, PtrKeyHash> LastAccessNo;
    unsigned long long MemAccessNo = 0;
    std::set<BasicBlock *> ProcessedBlocks;

    // 블록 순서 저장 (루프 감지용)
    std::unordered_map<BasicBlock *, int> BlockOrder;
    int order = 0;
    for (auto & BB : F)
    {
      BlockOrder[&BB] = order++;
    }

    // 루프 처리된 블록들 추적
    std::set<BasicBlock *> ProcessedLoops;

    for (auto & BB : F)
    {
      if (ProcessedBlocks.count(&BB)) continue;

      // 루프 헤더 감지 및 처리
      bool isLoopHeader = false;
      for (auto * Pred : predecessors(&BB))
      {
        if (BlockOrder[Pred] >= BlockOrder[&BB])
        {
          // Back-edge 발견 = 루프 헤더 (self-loop 포함)
          isLoopHeader = true;
          break;
        }
      }

      if (isLoopHeader && !ProcessedLoops.count(&BB))
      {
        ProcessedLoops.insert(&BB);

        // 1. Trip count 추출
        unsigned long long TripCount = extractTripCount(&BB);

        if (TripCount > 0)
        {
          // 2. 루프 블록들 수집
          std::set<BasicBlock *> LoopBlocks;
          collectLoopBlocks(&BB, LoopBlocks, BlockOrder);

          // 3. 반복 내 메모리 접근 시퀀스 구축
          auto IterSeq = buildIterationSequence(LoopBlocks, SE);

          if (!IterSeq.empty())
          {
            unsigned IterSize = IterSeq.size();

            // 4. 각 주소별 위치 수집
            std::unordered_map<PtrKey, std::vector<unsigned>, PtrKeyHash>
              PosMap;
            for (const auto & MA : IterSeq)
            {
              PosMap[MA.Key].push_back(MA.Position);
            }

            // 5. 재사용 패턴 분석 및 출력
            for (const auto & [Key, Positions] : PosMap)
            {
              // Intra-iteration 재사용
              if (Positions.size() > 1)
              {
                for (size_t i = 1; i < Positions.size(); ++i)
                {
                  unsigned long long RD = Positions[i] - Positions[i - 1];
                  printLoopRD(F.getName(), Key, RD, "intra-iter",
                              (unsigned)TripCount, TripCount,
                              IterSeq[Positions[i]].DebugLoc);
                }
              }

              // Loop-carried 재사용 (마지막 → 첫 번째)
              if (!Positions.empty() && TripCount > 1)
              {
                unsigned LastPos = Positions.back();
                unsigned FirstPos = Positions.front();

                // suffix = IterSize - LastPos - 1
                unsigned Suffix = IterSize - LastPos - 1;
                // prefix = FirstPos + 1
                unsigned Prefix = FirstPos + 1;

                unsigned long long RD = Suffix + Prefix;

                printLoopRD(F.getName(), Key, RD, "loop-carried",
                            (unsigned)(TripCount - 1), TripCount,
                            IterSeq[FirstPos].DebugLoc);
              }
            }

            // 6. 글로벌 상태 업데이트
            unsigned long long LoopTotalAccesses = IterSize * TripCount;

            // 마지막 접근 갱신
            for (size_t i = 0; i < IterSeq.size(); ++i)
            {
              const auto & MA = IterSeq[i];
              unsigned long long LastOccurrence =
                MemAccessNo + (TripCount - 1) * IterSize + i;
              LastAccessNo[MA.Key] = LastOccurrence;
            }

            MemAccessNo += LoopTotalAccesses;
          }

          // 루프 블록들 스킵
          for (auto * LBB : LoopBlocks)
          {
            ProcessedBlocks.insert(LBB);
          }
        }

        continue;
      }

      for (auto & I : BB)
      {
        //  분기 명령어 감지
        if (auto * BI = dyn_cast<BranchInst>(&I))
        {
          if (BI->isConditional() && BI->getNumSuccessors() >= 2)
          {
            // if/else 분기 처리
            std::vector<BasicBlock *> LeafBlocks;
            std::set<BasicBlock *> CollectVisited;

            for (unsigned i = 0; i < BI->getNumSuccessors(); ++i)
            {
              BasicBlock * SuccBB = BI->getSuccessor(i);
              collectLeafBranches(SuccBB, LastAccessNo, MemAccessNo, LeafBlocks,
                                  CollectVisited);
            }

            // 수집 과정에서 방문한 모든 블록을 처리됨으로 표시
            ProcessedBlocks.insert(CollectVisited.begin(),
                                   CollectVisited.end());

            // leaf 블록들도 처리됨으로 표시
            for (auto * LB : LeafBlocks)
            {
              ProcessedBlocks.insert(LB);
            }

            // 수집된 leaf 블록들 각각 측정
            std::vector<std::unordered_map<
              PtrKey, std::vector<unsigned long long>, PtrKeyHash>>
              BranchRDsAll;

            for (BasicBlock * LeafBB : LeafBlocks)
            {
              auto BranchLastAccess = LastAccessNo;
              unsigned long long BranchMemAccessNo = MemAccessNo;
              std::unordered_map<PtrKey, std::vector<unsigned long long>,
                                 PtrKeyHash>
                BranchAllRDs;
              std::set<BasicBlock *> Visited;

              measureBranchPathRD(LeafBB, BranchLastAccess, BranchMemAccessNo,
                                  BranchAllRDs, Visited, nullptr, SE);

              if (!BranchAllRDs.empty())
              {
                BranchRDsAll.push_back(BranchAllRDs);
              }
            }

            // 각 메모리 주소별로 분기 평균 RD 계산
            std::unordered_map<PtrKey, std::vector<unsigned long long>,
                               PtrKeyHash>
              RDsByKey;

            for (auto & BranchRDs : BranchRDsAll)
            {
              for (auto & [Key, RDList] : BranchRDs)
              {
                //  각 분기에서 해당 메모리의 모든 RD를 합산
                if (!RDList.empty())
                {
                  unsigned long long sumRD = 0;
                  for (auto rd : RDList)
                  {
                    sumRD += rd;
                  }
                  RDsByKey[Key].push_back(sumRD);  // ← 합계를 저장
                }
              }
            }

            // ★ 평균 RD 출력
            for (auto & [Key, RDs] : RDsByKey)
            {
              if (!RDs.empty())
              {
                unsigned long long Sum = 0;
                for (auto rd : RDs) Sum += rd;
                double AvgRD = (double)Sum / RDs.size();

                if (DILocation * Loc = I.getDebugLoc())
                {
                  errs() << F.getName() << ":" << Loc->getFilename() << ":"
                         << Loc->getLine();
                }
                else
                {
                  errs() << F.getName() << ":<ir>";
                }
                errs() << "  Branch-Avg-RD=" << format("%.2f", AvgRD)
                       << "  base=" << Key.Base << "  off=" << Key.Offset
                       << "  num_branches=" << RDs.size() << "\n";
              }
            }
            continue;  // 분기 처리 후 다음 명령어로
          }
        }

        // ★ 일반 메모리 접근 처리 (기존 로직)
        const Value * Ptr = nullptr;
        if (auto * L = dyn_cast<LoadInst>(&I))
        {
          Ptr = L->getPointerOperand();
        }
        else if (auto * S = dyn_cast<StoreInst>(&I))
        {
          Ptr = S->getPointerOperand();
        }
        else if (auto * MI = dyn_cast<MemIntrinsic>(&I))
        {
          if (auto * MT = dyn_cast<MemTransferInst>(MI))
          {
            Ptr = MT->getDest();
          }
          else if (auto * MS = dyn_cast<MemSetInst>(MI))
          {
            Ptr = MS->getDest();
          }
        }

        if (!Ptr) continue;

        unsigned long long CurNo = MemAccessNo++;
        PtrKey Key = normalizePtr(Ptr, SE);

        auto It = LastAccessNo.find(Key);
        if (It != LastAccessNo.end())
        {
          unsigned long long RD =
            (CurNo > It->second) ? (CurNo - It->second) : 0ULL;

          if (DILocation * Loc = I.getDebugLoc())
          {
            errs() << F.getName() << ":" << Loc->getFilename() << ":"
                   << Loc->getLine();
          }
          else
          {
            errs() << F.getName() << ":<ir>";
          }
          errs() << "  RD(mem-accesses)=" << RD << "  base=" << Key.Base
                 << "  off=" << Key.Offset << "\n";
        }

        LastAccessNo[Key] = CurNo;
      }
    }

    return PreservedAnalyses::all();
  }
};

}  // namespace

llvm::PassPluginLibraryInfo getReusePassPluginInfo()
{
  return {LLVM_PLUGIN_API_VERSION, "ReusePass", LLVM_VERSION_STRING,
          [](PassBuilder & PB) {
            PB.registerPipelineParsingCallback(
              [](StringRef Name, FunctionPassManager & FPM,
                 ArrayRef<PassBuilder::PipelineElement>) {
                if (Name == "reuse-pass")
                {
                  FPM.addPass(ReusePass());
                  return true;
                }
                return false;
              });
          }};
}

#ifndef LLVM_ATTRIBUTE_WEAK
#define LLVM_ATTRIBUTE_WEAK __attribute__((weak))
#endif
#ifndef LLVM_ATTRIBUTE_VISIBILITY_DEFAULT
#define LLVM_ATTRIBUTE_VISIBILITY_DEFAULT __attribute__((visibility("default")))
#endif

extern "C" LLVM_ATTRIBUTE_WEAK
  LLVM_ATTRIBUTE_VISIBILITY_DEFAULT ::llvm::PassPluginLibraryInfo
  llvmGetPassPluginInfo()
{
  return getReusePassPluginInfo();
}