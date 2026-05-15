#pragma once

#include <set>
#include <unordered_map>
#include <vector>

#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Operator.h"

#include "ReuseTypes.hpp"

// ---- 포인터 정규화 ----
int64_t getOffsetFromGEP(const llvm::GEPOperator * GEP,
                         llvm::ScalarEvolution * SE);

PtrKey normalizePtr(const llvm::Value * P,
                    llvm::ScalarEvolution * SE = nullptr);

// ---- 루프 분석 ----
unsigned long long extractTripCount(llvm::BasicBlock * Header);

void collectLoopBlocks(
  llvm::BasicBlock * Header, std::set<llvm::BasicBlock *> & LoopBlocks,
  const std::unordered_map<llvm::BasicBlock *, int> & BlockOrder);

std::vector<MemAccessInLoop>
buildIterationSequence(const std::set<llvm::BasicBlock *> & LoopBlocks,
                       llvm::ScalarEvolution * SE);