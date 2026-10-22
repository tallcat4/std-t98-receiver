param([string]$Compiler = '', [switch]$SkipTests)
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if (!$Compiler) {
        $found = Get-Command g++ -ErrorAction SilentlyContinue
        if ($found) { $Compiler = $found.Source }
        elseif (Test-Path 'C:\Strawberry\c\bin\g++.exe') { $Compiler = 'C:\Strawberry\c\bin\g++.exe' }
        else { throw 'Install a C++20 compiler, or build with CMake and Visual Studio 2022.' }
    }
    New-Item -ItemType Directory -Force build | Out-Null
    & ./setup-fftw.ps1
    $flags = @('-std=c++20', '-O2', '-static', '-Ibuild/fftw', '-Isrc/include', '-Isrc', '-Ivendor/spear/apps/std_t98', '-Ivendor/spear/dsp/include')
    $sources = @('src/input.cpp', 'src/output.cpp', 'vendor/spear/apps/std_t98/receiver.cpp',
        'vendor/spear/apps/std_t98/protocol/protocol.cpp', 'vendor/spear/apps/std_t98/dsp/rrc.cpp',
        'vendor/spear/apps/std_t98/dsp/symbol_sync.cpp', 'vendor/spear/apps/std_t98/dsp/sync_correlator.cpp',
        'vendor/spear/dsp/src/fir.cpp', 'vendor/spear/dsp/src/fftw_planner.cpp')
    & $Compiler @flags src/main.cpp @sources build/fftw/libfftw3f-3.dll -lshell32 -o build/std-t98.exe
    if ($LASTEXITCODE) { throw 'Receiver compilation failed' }
    if (!$SkipTests) {
        & $Compiler @flags '-Ivendor/spear/apps/std_t98/tests' tests/native/test_main.cpp vendor/spear/dsp/src/channelizer.cpp @sources build/fftw/libfftw3f-3.dll -o build/t98-tests.exe
        if ($LASTEXITCODE) { throw 'Test compilation failed' }
        Push-Location build
        try { & ./t98-tests.exe; if ($LASTEXITCODE) { throw 'Native tests failed' } } finally { Pop-Location }
    }
    Write-Host 'Ready: build\std-t98.exe'
} finally { Pop-Location }
