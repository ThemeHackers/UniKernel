#!/usr/bin/env pwsh

Write-Host "`n╔══════════════════════════════════════════════════════════╗"
Write-Host "║  Building CUDA Kernel Test Suite                         ║"
Write-Host "╚══════════════════════════════════════════════════════════╝`n"

$gpu_check = & nvidia-smi --query-gpu=name --format=csv,noheader 2>$null
if (-not $gpu_check) {
    Write-Host "❌ No NVIDIA GPU detected!" -ForegroundColor Red
    exit 1
}

Write-Host "GPU: $gpu_check" -ForegroundColor Green

$script_dir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root_dir = Split-Path -Parent $script_dir
$build_dir = Join-Path $root_dir "build_test"

if (-not (Test-Path $build_dir)) {
    New-Item -ItemType Directory -Path $build_dir | Out-Null
}

Write-Host "[1/3] Activating MSVC compiler..." -ForegroundColor Cyan

$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    Write-Host "❌ MSVC not found at: $vcvars" -ForegroundColor Red
    exit 1
}

cmd /c "`"$vcvars`" && set" | ForEach-Object {
    if ($_ -match '=') {
        $name, $value = $_ -split '=', 2
        [Environment]::SetEnvironmentVariable($name, $value)
    }
}

Write-Host "[2/3] Compiling CUDA kernel modules..." -ForegroundColor Cyan

$sources = @(
    "graphics_kernels.cu",
    "crypto_kernels.cu",
    "math_kernels.cu",
    "physics_kernels.cu",
    "signal_kernels.cu",
    "bench_kernels.cu"
)

$compile_success = $true
foreach ($source in $sources) {
    $src_path = Join-Path $root_dir "src\$source"
    $obj_path = Join-Path $build_dir ($source -replace "\.cu$", ".o")
    
    Write-Host "  Compiling $source..." -NoNewline
    
    & nvcc -c $src_path -o $obj_path `
        -I"$root_dir\include" `
        -gencode arch=compute_75,code=sm_75 `
        -std=c++17 `
        -allow-unsupported-compiler 2>&1 | Out-Null
    
    if ($LASTEXITCODE -eq 0) {
        Write-Host " ✓" -ForegroundColor Green
    } else {
        Write-Host " ✗" -ForegroundColor Red
        $compile_success = $false
    }
}

if (-not $compile_success) {
    Write-Host "`n❌ Compilation failed!" -ForegroundColor Red
    exit 1
}

Write-Host "[3/3] Linking executable..." -ForegroundColor Cyan

$obj1 = Join-Path $build_dir "graphics_kernels.o"
$obj2 = Join-Path $build_dir "crypto_kernels.o"
$obj3 = Join-Path $build_dir "math_kernels.o"
$obj4 = Join-Path $build_dir "physics_kernels.o"
$obj5 = Join-Path $build_dir "signal_kernels.o"
$obj6 = Join-Path $build_dir "bench_kernels.o"
$object_files = $obj1, $obj2, $obj3, $obj4, $obj5, $obj6

$test_file = Join-Path $script_dir "test_all.cu"
$exe_path = Join-Path $build_dir "test_all.exe"

& nvcc $test_file $object_files -o $exe_path `
    -I"$root_dir\include" `
    -gencode arch=compute_75,code=sm_75 `
    -std=c++17 `
    -allow-unsupported-compiler 2>&1 | Out-Null

if ($LASTEXITCODE -ne 0) {
    Write-Host "❌ Linking failed!" -ForegroundColor Red
    exit 1
}

Write-Host "`n✓ Build successful! Running tests...`n" -ForegroundColor Green

& $exe_path

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n✓ All kernel modules tested successfully!" -ForegroundColor Green
    exit 0
} else {
    Write-Host "`n❌ Tests failed!" -ForegroundColor Red
    exit 1
}
