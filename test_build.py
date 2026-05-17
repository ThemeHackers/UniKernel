#!/usr/bin/env python3
import os
import sys
import hashlib
from colorama import init, Fore, Back, Style
init(autoreset=True)
SUCCESS = Fore.GREEN + Style.BRIGHT
ERROR = Fore.RED + Style.BRIGHT
WARNING = Fore.YELLOW + Style.BRIGHT
INFO = Fore.CYAN + Style.BRIGHT
HEADER = Fore.MAGENTA + Style.BRIGHT
NEUTRAL = Fore.WHITE
def print_header(title):
    width = 80
    padding = (width - len(title) - 2) // 2
    print(f"\n{HEADER}{'='*width}")
    print(f"{HEADER}{' '*padding} {title} {' '*padding}")
    print(f"{HEADER}{'='*width}{Style.RESET_ALL}")
def print_test_title(test_num, title):
    print(f"\n{INFO}╔═ TEST {test_num}: {title} {'='*(60-len(title)-len(str(test_num)))}{Style.RESET_ALL}")
def print_item(status, text, extra=""):
    if status:
        indicator = f"{SUCCESS}✓{Style.RESET_ALL}"
    else:
        indicator = f"{ERROR}✗{Style.RESET_ALL}"
    if extra:
        print(f"  {indicator} {text:<50} {INFO}{extra}{Style.RESET_ALL}")
    else:
        print(f"  {indicator} {text}")
def print_section(text):
    print(f"{INFO}  ├─ {text}{Style.RESET_ALL}")
def print_summary_line(test_name, passed):
    status = f"{SUCCESS}✓ PASS{Style.RESET_ALL}" if passed else f"{ERROR}✗ FAIL{Style.RESET_ALL}"
    bar = "─" * (65 - len(test_name))
    print(f"  {status}  {test_name} {bar}")
def test_main_cu_location():
    main_cu_candidates = [
        "main.cu",
        "UniAccel/main.cu",
        os.path.join(os.getcwd(), "UniAccel", "main.cu")
    ]
    print_test_title(1, "main.cu Auto-Detection")
    print(f"{INFO}📁 Working Directory{Style.RESET_ALL}")
    print(f"   {os.getcwd()}\n")
    main_cu_path = None
    for path in main_cu_candidates:
        exists = os.path.exists(path)
        if exists:
            size = os.path.getsize(path)
            print_item(True, f"{path:<48}", f"[{size:,} bytes]")
            if not main_cu_path:
                main_cu_path = path
        else:
            print_item(False, path)
    print(f"\n{INFO}  └─ Result{Style.RESET_ALL}")
    if main_cu_path:
        with open(main_cu_path, 'r') as f:
            content = f.read()
        sha256_hash = hashlib.sha256(content.encode()).hexdigest()
        print_item(True, f"Found at: {main_cu_path}")
        print(f"     • Size: {len(content):,} bytes")
        print(f"     • SHA256: {sha256_hash[:48]}...")
        print(f"     • Preview: {content[:70].replace(chr(10), ' ')}...")
        return True, main_cu_path
    else:
        print_item(False, "NOT FOUND in any location")
        return False, None
def test_include_dirs():
    print_test_title(2, "Include Directories Auto-Detection")
    inc_dirs_candidates = []
    for base_dir in [os.getcwd(), os.path.join(os.getcwd(), "UniAccel")]:
        for sub_dir in [".", "include", "src"]:
            path = base_dir if sub_dir == "." else os.path.join(base_dir, sub_dir)
            exists = os.path.exists(path)
            display_path = path.replace(os.getcwd(), ".")
            print_item(exists, display_path)
            if exists:
                inc_dirs_candidates.append(path)
    inc_dirs = list(dict.fromkeys(inc_dirs_candidates))
    print(f"\n{INFO}  └─ Result{Style.RESET_ALL}")
    print_item(True, f"Found {len(inc_dirs)} include directories")
    for i, path in enumerate(inc_dirs, 1):
        rel_path = path.replace(os.getcwd(), ".")
        print(f"     {i}. {rel_path}")
    return len(inc_dirs) > 0, inc_dirs
def test_header_files(inc_dirs):
    print_test_title(3, "CUDA Header Files")
    cuda_headers = [
        "bench_kernels.cuh",
        "crypto_kernels.cuh",
        "cuda_utils.cuh",
        "graphics_kernels.cuh",
        "math_kernels.cuh",
        "signal_kernels.cuh"
    ]
    found_count = 0
    for header in cuda_headers:
        found = False
        for inc_dir in inc_dirs:
            header_path = os.path.join(inc_dir, header)
            if os.path.exists(header_path):
                rel_dir = inc_dir.replace(os.getcwd(), ".")
                size = os.path.getsize(header_path)
                print_item(True, f"{header:<35}", f"[{size:,} bytes]")
                found = True
                found_count += 1
                break
        if not found:
            print_item(False, header)
    print(f"\n{INFO}  └─ Result{Style.RESET_ALL}")
    print_item(found_count == len(cuda_headers), f"Found {found_count}/{len(cuda_headers)} headers")
    return found_count == len(cuda_headers), found_count, len(cuda_headers)
def test_source_files(inc_dirs):
    print_test_title(4, "CUDA Source Files")
    cuda_sources = [
        "bench_kernels.cu",
        "crypto_kernels.cu",
        "graphics_kernels.cu",
        "math_kernels.cu",
        "physics_kernels.cu",
        "signal_kernels.cu"
    ]
    found_count = 0
    for source in cuda_sources:
        found = False
        for inc_dir in inc_dirs:
            src_path = inc_dir if inc_dir.endswith("src") else os.path.join(inc_dir, "src")
            source_path = os.path.join(src_path, source)
            if os.path.exists(source_path):
                rel_path = source_path.replace(os.getcwd(), ".")
                size = os.path.getsize(source_path)
                print_item(True, f"{source:<35}", f"[{size:,} bytes]")
                found = True
                found_count += 1
                break
        if not found:
            print_item(False, source)
    print(f"\n{INFO}  └─ Result{Style.RESET_ALL}")
    print_item(found_count == len(cuda_sources), f"Found {found_count}/{len(cuda_sources)} sources")
    return found_count == len(cuda_sources), found_count, len(cuda_sources)
def test_project_structure(main_cu_path, inc_dirs):
    print_test_title(5, "Project Structure")
    checks = [
        ("main.cu location", os.path.exists(main_cu_path) if main_cu_path else False),
        ("Include directories", len(inc_dirs) > 0),
        ("Build test directory", os.path.exists(os.path.join(os.getcwd(), "UniAccel", "build_test"))),
        ("Test directory", os.path.exists(os.path.join(os.getcwd(), "UniAccel", "test"))),
    ]
    for check_name, check_result in checks:
        print_item(check_result, check_name)
    print(f"\n{INFO}  └─ Result{Style.RESET_ALL}")
    all_passed = all(result for _, result in checks)
    print_item(all_passed, "Project structure is valid")
    return all_passed
def main():
    print(f"\n{HEADER}{'╔'+'═'*78+'╗':^80}")
    print(f"{HEADER}{'║'} {'🚀 UNIKERNEL BUILD SYSTEM TEST SUITE 🚀':^76} {'║'}")
    print(f"{HEADER}{'╚'+'═'*78+'╝':^80}{Style.RESET_ALL}")
    os.chdir(os.path.dirname(os.path.abspath(__file__)) or ".")
    test1_pass, main_cu_path = test_main_cu_location()
    test2_pass, inc_dirs = test_include_dirs()
    test3_pass, header_found, header_total = test_header_files(inc_dirs)
    test4_pass, source_found, source_total = test_source_files(inc_dirs)
    test5_pass = test_project_structure(main_cu_path, inc_dirs) if test1_pass else False
    print_header("FINAL SUMMARY")
    tests = [
        ("main.cu Detection", test1_pass),
        ("Include Directories", test2_pass),
        (f"CUDA Headers ({header_found}/{header_total})", test3_pass),
        (f"CUDA Sources ({source_found}/{source_total})", test4_pass),
        ("Project Structure", test5_pass),
    ]
    print(f"{HEADER}Test Results:{Style.RESET_ALL}")
    for test_name, result in tests:
        print_summary_line(test_name, result)
    all_passed = all(result for _, result in tests)
    print(f"\n{HEADER}{'─'*80}{Style.RESET_ALL}")
    if all_passed:
        print(f"\n{SUCCESS}{'╔'+'═'*78+'╗':^80}")
        print(f"{SUCCESS}{'║'} {'✅ ALL TESTS PASSED! BUILD SYSTEM IS READY 🎉':^76} {'║'}")
        print(f"{SUCCESS}{'╚'+'═'*78+'╝':^80}{Style.RESET_ALL}\n")
        return 0
    else:
        print(f"\n{ERROR}{'╔'+'═'*78+'╗':^80}")
        print(f"{ERROR}{'║'} {'❌ SOME TESTS FAILED! PLEASE FIX THE ISSUES':^76} {'║'}")
        print(f"{ERROR}{'╚'+'═'*78+'╝':^80}{Style.RESET_ALL}\n")
        return 1
if __name__ == "__main__":
    sys.exit(main())
