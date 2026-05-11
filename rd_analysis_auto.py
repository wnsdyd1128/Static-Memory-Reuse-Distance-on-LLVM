# rd_analysis.py
import subprocess
import os
import re
import json
from pathlib import Path
from collections import defaultdict
def _find_tool(versioned, fallback):
    """versioned(e.g. clang-14) 우선 탐색, 없으면 fallback(e.g. clang) 사용. 둘 다 없으면 RuntimeError."""
    for name in (versioned, fallback):
        if subprocess.run(["which", name], capture_output=True).returncode == 0:
            logger.info(f"사용할 도구: {name}")
            return name
    raise RuntimeError(f"{versioned} 또는 {fallback} 을 찾을 수 없습니다. LLVM이 설치되어 있는지 확인하세요.")


class RDAnalyzer:
    def __init__(self, llvm_build_dir=None):
        self.clang = _find_tool("clang-14", "clang")
        self.opt   = _find_tool("opt-14",   "opt")

        if llvm_build_dir is None:
            # 스크립트 위치 기준으로 build 디렉토리 경로 설정
            script_dir = Path(__file__).parent
            self.build_dir = script_dir / "build"
        else:
            self.build_dir = Path(llvm_build_dir)

        self.build_dir = self.build_dir.resolve()

        import platform
        plugin_name = "libReusePass.dylib" if platform.system() == "Darwin" else "libReusePass.so"
        self.plugin_path = self.build_dir / plugin_name
        
    def c_to_ir(self, c_file):
        """C 파일을 LLVM IR로 변환"""
        ir_file = Path(c_file).with_suffix('.ll')
                self.clang, '-O0', '-Xclang', '-disable-O0-optnone',
        subprocess.run(cmd, check=True)
        return ir_file
    
    def run_reuse_pass(self, ir_file):
        """ReusePass 실행하여 RD 데이터 추출"""
        
        ir_file_abs = Path(ir_file).resolve()
        cmd = [
            self.opt, '-load-pass-plugin', str(self.plugin_path),
            '-passes=function(reuse-pass)', str(ir_file_abs), '-disable-output'
        ]
        result = subprocess.run(cmd, capture_output=True, text=True, cwd=self.build_dir)
        return result.stderr  # RD 정보는 stderr로 출력됨
    
    def parse_rd_output(self, output):
        """RD 출력 파싱하여 함수별, 메모리 주소별로 그룹화"""
        rd_data = defaultdict(lambda: defaultdict(list))
        
        for line in output.strip().split('\n'):
            if 'RD(mem-accesses)=' in line or 'Branch-Avg-RD=' in line:
                # 함수명 추출
                func_match = re.match(r'(\w+):', line)
                func_name = func_match.group(1) if func_match else "unknown"
                
                if 'RD(mem-accesses)=' in line:
                    # 일반 RD 파싱
                    match = re.search(r'RD\(mem-accesses\)=(\d+).*base=([^\s]+).*off=(\d+)', line)
                    if match:
                        original_rd = int(match.group(1))
                        adjusted_rd = original_rd
                        base = match.group(2)
                        offset = int(match.group(3))
                        key = f"{base}+{offset}"
                        rd_data[func_name][key].append(adjusted_rd)
                elif 'Branch-Avg-RD=' in line:
                    # 분기 평균 RD도 파싱 (선택적)
                    match = re.search(r'Branch-Avg-RD=([\d.]+).*base=([^\s]+).*off=(\d+)', line)
                    if match:
                        avg_rd = float(match.group(1))
                        base = match.group(2)
                        offset = int(match.group(3))
                        key = f"{base}+{offset}"
                        # 분기 평균도 RD 데이터에 포함
                        rd_data[func_name][key].append(int(avg_rd))  # 정수로 변환하거나 float 그대로
        
        return rd_data
    
    def calculate_averages(self, rd_data):
        """함수별, 메모리 주소별 RD 평균 계산"""
        averages = {}
        for func_name, addr_data in rd_data.items():
            averages[func_name] = {}
            for addr, rd_values in addr_data.items():
                if rd_values:
                    avg = sum(rd_values) / len(rd_values)
                    averages[func_name][addr] = round(avg, 2)
        return averages
    
    def calculate_cachefriendly_score(self, rd_data, averages):
        """Cache Friendly Score 계산"""
        cachefriendly_scores = {}
        
        for func_name, addr_data in rd_data.items():
            numerator = 0
            denominator = 0
            
            for addr, rd_values in addr_data.items():
                # N(x_i): 접근 횟수
                N_xi = len(rd_values)
                
                # δavg(x_i): 평균 RD (이미 계산된 값 사용)
                delta_avg_xi = averages[func_name][addr]
                
                # 분자: Σ_i (N(x_i) - 1)
                numerator += (N_xi - 1)
                
                # 분모: Σ_i (δavg(x_i) * (N(x_i) - 1))
                denominator += delta_avg_xi * (N_xi - 1)
            
            # Score_cachefriendly 계산
            if denominator > 0:
                score = numerator / denominator
                cachefriendly_scores[func_name] = round(score, 6)
            else:
                cachefriendly_scores[func_name] = 0.0
        
        return cachefriendly_scores
    
    def analyze_file(self, c_file):
        """단일 C 파일 전체 분석"""
        print(f"분석 중: {c_file}")
        
        # 1. C → IR 변환
        ir_file = self.c_to_ir(c_file)
        print(f"IR 파일 생성: {ir_file}")
        
        # 2. ReusePass 실행
        rd_output = self.run_reuse_pass(ir_file)
        print(f"ReusePass 출력 길이: {len(rd_output)}")
        print(f"ReusePass 출력 내용:\n{rd_output}")
        
        # 3. 결과 파싱
        rd_data = self.parse_rd_output(rd_output)
        print(f"파싱된 RD 데이터: {dict(rd_data)}")
        
        # 4. 평균 계산
        averages = self.calculate_averages(rd_data)
        print(f"계산된 평균: {averages}")
        
        # 5. Cache Friendly Score 계산
        cachefriendly_scores = self.calculate_cachefriendly_score(rd_data, averages)
        print(f"계산된 Cache Friendly Scores: {cachefriendly_scores}")
        
        return {
            'file': str(c_file),
            'functions': rd_data,  # 함수별로 구조화
            'averages': averages,   # 함수별 평균
            'cachefriendly_scores': cachefriendly_scores  # 함수별 Cache Friendly Score
        }
    
    def batch_analyze(self, c_files):
        """여러 C 파일 일괄 분석"""
        results = []
        for c_file in c_files:
            try:
                result = self.analyze_file(c_file)
                results.append(result)
            except Exception as e:
                print(f"오류: {c_file} - {e}")
        
        return results
    
    def generate_report(self, results, output_file="rd_analysis_report.json"):
        """분석 결과를 JSON으로 저장"""
        with open(output_file, 'w') as f:
            json.dump(results, f, indent=2)
        print(f"결과 저장: {output_file}")

# 사용 예시, task C 파일을 넣을 것
if __name__ == "__main__":
    analyzer = RDAnalyzer()
    
    # 단일 파일 분석
    # result = analyzer.analyze_file("test.c")
    import glob
    c_files = glob.glob("tasks/*.c")
    
    if c_files:
        print(f"발견된 C 파일들: {c_files}")
        results = analyzer.batch_analyze(c_files)
        analyzer.generate_report(results)
    else:
        print("분석할 .c 파일이 없습니다.")
