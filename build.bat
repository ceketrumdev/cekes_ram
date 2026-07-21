@echo off
echo =====================================================================
echo  CEKE'S RAM TEST v1.2 - Build Script (MSVC x64 AVX2)
echo =====================================================================

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" 2>nul
if errorlevel 1 (
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" 2>nul
)
if errorlevel 1 (
    echo [ERROR] Could not find Visual Studio environment.
    pause
    exit /b 1
)

if not exist bin mkdir bin

echo [+] Compiling src\ram_stress_diag.c (10 Stress Modules + KMDF Driver Bridge) ...
cl.exe /O2 /Oi /Ot /arch:AVX2 /W4 /D_CRT_SECURE_NO_WARNINGS /I driver src\ram_stress_diag.c /Fe:bin\ram_stress_diag.exe /link advapi32.lib

if errorlevel 1 (
    echo [FAIL] User-space compilation failed.
    pause
    exit /b 1
)

del ram_stress_diag.obj 2>nul

echo.
echo [OK] Build successful: bin\ram_stress_diag.exe
echo.
echo [NOTE KERNEL DRIVER]
echo  - Source code for KMDF driver is in driver\cekes_ram_drv.c
echo  - To build driver: Open Visual Studio WDK environment and run 'msbuild driver\cekes_ram_drv.vcxproj'
echo  - The user-mode engine will automatically detect if cekes_ram_drv.sys is loaded.
echo  - If driver is not loaded, it falls back to native Windows APIs seamlessly.
echo.
pause
