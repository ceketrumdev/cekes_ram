@echo off
:: =====================================================================
::  UNINSTALL.BAT - Désinstallateur Automatisé Ceke's RAM Test & Driver
:: =====================================================================
TITLE Ceke's RAM Test - Désinstallateur

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] Demande d'élévation Administrateur...
    powershell -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

echo =====================================================================
echo  DÉSINSTALLATION DE CEKE'S RAM TEST
echo =====================================================================
echo.

set "TARGET_DIR=%ProgramFiles%\Ceke's RAM Test"

echo [+] Arrêt et suppression du service pilote Kernel (CekesRamDriver)...
sc.exe stop CekesRamDriver >nul 2>&1
sc.exe delete CekesRamDriver >nul 2>&1

echo [+] Suppression des raccourcis Bureau et Menu Démarrer...
if exist "%Public%\Desktop\Ceke's RAM Test.lnk" del /F /Q "%Public%\Desktop\Ceke's RAM Test.lnk"
if exist "%ProgramData%\Microsoft\Windows\Start Menu\Programs\Ceke's RAM Test.lnk" del /F /Q "%ProgramData%\Microsoft\Windows\Start Menu\Programs\Ceke's RAM Test.lnk"

echo [+] Suppression des fichiers d'installation...
if exist "%TARGET_DIR%" rmdir /S /Q "%TARGET_DIR%"

echo.
echo =====================================================================
echo  DÉSINSTALLATION TERMINÉE AVEC SUCCÈS !
echo =====================================================================
echo.
pause
