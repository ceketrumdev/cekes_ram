@echo off
echo =====================================================================
echo  CEKE'S RAM TEST - Build Script (MSVC x64 AVX2)
echo =====================================================================

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" 2>nul
if errorlevel 1 (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" 2>nul
)
if errorlevel 1 (
    echo [ERROR] Could not find Visual Studio. Install VS 2019+ with MSVC tools.
    pause
    exit /b 1
)

echo [+] Compiling ram_stress_diag.c with /O2 /Oi /Ot /arch:AVX2 ...
cl.exe /O2 /Oi /Ot /arch:AVX2 /W4 /D_CRT_SECURE_NO_WARNINGS src\ram_stress_diag.c /Fe:bin\ram_stress_diag.exe /link advapi32.lib

if errorlevel 1 (
    echo [FAIL] Compilation failed.
    pause
    exit /b 1
)

echo.
echo [OK] Build successful: bin\ram_stress_diag.exe
echo.
pause
