#pragma once

#include <cstdint>
#include <map>
#include <vector>

#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Value.h"

// ---- 포인터 키: (정규화된 base, 상수 오프셋) ----
struct PtrKey
{
  const llvm::Value * Base;
  int64_t Offset;

  bool operator==(const PtrKey & O) const
  {
    return Base == O.Base && Offset == O.Offset;
  }
};

struct PtrKeyHash
{
  size_t operator()(const PtrKey & K) const noexcept
  {
    return std::hash<const void *>()(K.Base) ^
           (std::hash<int64_t>()(K.Offset) * 1315423911u);
  }
};

// ---- 반복 내 메모리 접근 정보 ----
struct MemAccessInLoop
{
  llvm::Instruction * Inst;
  PtrKey Key;
  int64_t SCEVStart;  // 루프 인덱스 시작값 (arr[i] → 0, arr[i+1] → 1)
  int64_t SCEVStep;   // 루프 인덱스 stride  (arr[i] → 1, arr[2] → 0)
  unsigned Position;  // 반복 내 위치 (0-based)
  llvm::DILocation * DebugLoc;
};

// ---- RD 히스토그램: RD 값 → 빈도 ----
struct RDHist
{
  std::map<int64_t, int64_t> hist;  // RD → 빈도
  int64_t coldCount = 0;
};

// ---- 크로스 블록 Cold Miss 보정용 ----
struct BlockAccessInfo
{
  const llvm::Value * Base;
  int64_t SCEVStart;
  int64_t SCEVStep;
  int64_t TripCount;
  unsigned FirstPosInIter;
  unsigned LastPosInIter;
  unsigned IterSize;
  int64_t BlockStart;  // MemAccessNo 기준 블록 시작
  int64_t BlockEnd;    // MemAccessNo 기준 블록 끝
};
