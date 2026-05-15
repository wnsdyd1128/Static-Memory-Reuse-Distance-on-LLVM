#pragma once

#include <set>
#include <unordered_map>
#include <vector>

#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/IR/BasicBlock.h"

#include "ReuseTypes.hpp"

bool isMergeBlock(llvm::BasicBlock * BB, llvm::BasicBlock * BranchBlock);

void collectLeafBranches(
  llvm::BasicBlock * BB,
  std::unordered_map<PtrKey, unsigned long long, PtrKeyHash> LastAccessNo,
  unsigned long long MemAccessNo,
  std::vector<llvm::BasicBlock *> & LeafBlocks,
  std::set<llvm::BasicBlock *> & Visited);

void measureBranchPathRD(
  llvm::BasicBlock * BB,
  std::unordered_map<PtrKey, unsigned long long, PtrKeyHash> & LastAccessNo,
  unsigned long long & MemAccessNo,
  std::unordered_map<PtrKey, std::vector<unsigned long long>, PtrKeyHash> &
    AllRDs,
  std::set<llvm::BasicBlock *> & Visited, llvm::BasicBlock * StartBlock,
  llvm::ScalarEvolution * SE);