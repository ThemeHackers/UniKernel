@echo off
setlocal enabledelayedexpansion

echo.
echo ╔══════════════════════════════════════════════════════════╗
echo ║  Building CUDA Kernel Test Suite                         ║
echo ╚══════════════════════════════════════════════════════════╝
echo.

echo [1/3] Activating MSVC compiler...
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

if not exist .\build_test mkdir .\build_test

echo [2/3] Compiling test_all.cu...
cd /d %~dp0\..

nvcc -c src/graphics_kernels.cu -o build_test/graphics_kernels.o ^
     -I./include -gencode arch=compute_75,code=sm_75 -std=c++17 -allow-unsupported-compiler || goto :error

nvcc -c src/crypto_kernels.cu -o build_test/crypto_kernels.o ^
     -I./include -gencode arch=compute_75,code=sm_75 -std=c++17 -allow-unsupported-compiler || goto :error

nvcc -c src/math_kernels.cu -o build_test/math_kernels.o ^
     -I./include -gencode arch=compute_75,code=sm_75 -std=c++17 -allow-unsupported-compiler || goto :error

nvcc -c src/physics_kernels.cu -o build_test/physics_kernels.o ^
     -I./include -gencode arch=compute_75,code=sm_75 -std=c++17 -allow-unsupported-compiler || goto :error

nvcc -c src/signal_kernels.cu -o build_test/signal_kernels.o ^
     -I./include -gencode arch=compute_75,code=sm_75 -std=c++17 -allow-unsupported-compiler || goto :error

nvcc -c src/bench_kernels.cu -o build_test/bench_kernels.o ^
     -I./include -gencode arch=compute_75,code=sm_75 -std=c++17 -allow-unsupported-compiler || goto :error

echo [3/3] Linking and creating executable...
nvcc test/test_all.cu ^
     build_test/graphics_kernels.o ^
     build_test/crypto_kernels.o ^
     build_test/math_kernels.o ^
     build_test/physics_kernels.o ^
     build_test/signal_kernels.o ^
     build_test/bench_kernels.o ^
     -o build_test/test_all.exe ^
     -I./include -gencode arch=compute_75,code=sm_75 -std=c++17 -allow-unsupported-compiler || goto :error

echo.
echo ✓ Build successful! Running tests...
echo.

cd /d %~dp0\build_test
test_all.exe

if %errorlevel% neq 0 (
    echo.
    echo ❌ Tests failed!
    exit /b 1
)

echo.
goto :success

:error
echo.
echo ❌ Build failed!
exit /b 1

:success
echo.
echo ✓ All kernel modules tested successfully!
