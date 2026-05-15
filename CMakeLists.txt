cmake_minimum_required(VERSION 3.20)
project(ReusePass LANGUAGES C CXX)

# LLVM 찾기 (configure 시: cmake -DLLVM_DIR=$(llvm-config --cmakedir) ..)
find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM_DIR: ${LLVM_DIR}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# LLVM 헤더 경로 포함
include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# MODULE로 빌드 (플러그인 방식)
add_library(ReusePass MODULE
  ReusePass.cpp
  LoopAnalysis.cpp
  BranchAnalysis.cpp
)

# 가시성 설정
set_target_properties(ReusePass PROPERTIES
  CXX_VISIBILITY_PRESET default
  VISIBILITY_INLINES_HIDDEN NO
)

# 플랫폼별 설정
if(APPLE)
  # macOS: undefined symbols 허용 (opt가 제공)
  set_target_properties(ReusePass PROPERTIES
    MACOSX_RPATH ON
    LINK_FLAGS "-undefined dynamic_lookup"
  )
else()
  # Linux: undefined symbols 허용 (opt가 제공)
  target_link_options(ReusePass PRIVATE "-Wl,--allow-shlib-undefined")
endif()

