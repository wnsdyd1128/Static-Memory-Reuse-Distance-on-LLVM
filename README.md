# LLVM Reuse Distance Analysis Pass

LLVM 기반의 메모리 재사용 거리(Reuse Distance) 분석 도구입니다. C 코드의 메모리 접근 패턴을 분석하여 캐시 성능 최적화에 활용할 수 있는 정보를 제공합니다.

## 프로젝트 개요

이 프로젝트는 LLVM의 커스텀 패스를 활용하여 프로그램의 메모리 접근 패턴을 분석합니다. 각 메모리 접근에 대한 재사용 거리(Reuse Distance)를 측정합니다.

### 주요 기능
- ✅ C 코드의 메모리 접근 패턴 자동 분석
- ✅ Reuse Distance (RD) 측정 및 통계 제공
- ✅ Cache Friendly Score 계산
- ✅ Docker를 통한 간편한 실행 환경
- ✅ JSON 형식의 분석 결과 출력 및 로그 파일 저장
- ✅ 포인터 별칭 분석 및 GEP 오프셋 추적
- ✅ RTEMS 타겟 컴파일 옵션 지원 (`--target=sparc-rtems6`)
- ✅ `clang-14`/`opt-14` 또는 `clang`/`opt` 자동 탐지

## 🔨 빌드

### 요구 사항
- CMake 3.20 이상
- LLVM 14 (`clang-14`, `opt-14`, `llvm-config`)

### ReusePass 빌드

```bash
mkdir build && cd build
cmake -DLLVM_DIR=$(llvm-config --cmakedir) ..
make
```

빌드 성공 시 `build/libReusePass.so` (macOS: `libReusePass.dylib`) 가 생성됩니다.

## 🚀 빠른 시작

### 1. Docker 이미지 빌드
```bash
docker build -t llvm-reuse-pass:latest .
```

### 2. 분석할 C 코드 준비
`tasks` 디렉토리에 분석할 C 파일(`.c`)을 배치하세요.

### 3. Docker 컨테이너 실행
```bash
docker run -it --rm \
  -v $(pwd)/tasks:/app/tasks \
  llvm-reuse-pass:latest
```

```bash
python3 rd_analysis_auto.py
```

## ⚙️ 옵션

| 옵션 | 설명 | 기본값 |
|---|---|---|
| `--input` | 분석할 C 파일 경로 또는 glob 패턴 | `tasks/*.c` |
| `--output` | 결과 JSON 파일 경로 | `rd_analysis_report.json` |
| `--rtems` | RTEMS 컴파일 옵션 사용 (`--target=sparc-rtems6`) | 미사용 |

```bash
# 특정 파일 분석
python3 rd_analysis_auto.py --input path/to/file.c --output result.json

# RTEMS 타겟으로 분석
python3 rd_analysis_auto.py --rtems --output result_rtems.json

# glob 패턴으로 여러 파일 분석
python3 rd_analysis_auto.py --input "src/**/*.c"
```

실행 결과는 `--output`으로 지정한 경로에 JSON으로 저장되며, 동일한 이름의 `.log` 파일에도 함께 기록됩니다.
예: `--output result.json` → `result.json` + `result.log`
