@echo off
:: =====================================================================
::  INSTALL.BAT - Installateur Automatisé Ceke's RAM Test & Driver Kernel
:: =====================================================================
TITLE Ceke's RAM Test v1.2 - Installateur Administrateur

:: Auto-élévation Administrateur
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [!] Demande d'élévation Administrateur...
    powershell -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

echo =====================================================================
echo  INSTALLATION DE CEKE'S RAM TEST v1.2
echo =====================================================================
echo.

set "TARGET_DIR=%ProgramFiles%\Ceke's RAM Test"

echo [+] Création du dossier d'installation : "%TARGET_DIR%"
if not exist "%TARGET_DIR%" mkdir "%TARGET_DIR%"

echo [+] Copie des fichiers de l'application...
copy /Y "%~dp0bin\ram_stress_diag.exe" "%TARGET_DIR%\ram_stress_diag.exe" >nul
if exist "%~dp0driver\cekes_ram_drv.sys" (
    copy /Y "%~dp0driver\cekes_ram_drv.sys" "%TARGET_DIR%\cekes_ram_drv.sys" >nul
)

echo [+] Installation du Service Pilote Noyau (CekesRamDriver)...
sc.exe stop CekesRamDriver >nul 2>&1
sc.exe delete CekesRamDriver >nul 2>&1

if exist "%TARGET_DIR%\cekes_ram_drv.sys" (
    sc.exe create CekesRamDriver type= kernel binPath= "%TARGET_DIR%\cekes_ram_drv.sys" start= auto >nul
    sc.exe start CekesRamDriver >nul 2>&1
    if errorlevel 1 (
        echo [!] Note: Le pilote kernel nécessite le mode Test-Signing sous Windows 64-bit.
        echo [!] Si le driver ne démarre pas, le logiciel basculera en mode Fallback Win32 sans problème.
    ) else (
        echo [OK] Pilote Kernel CekesRamDriver démarré avec succès !
    )
) else (
    echo [!] Fichier cekes_ram_drv.sys non présent. Mode Fallback Win32 activé par défaut.
)

echo [+] Création des raccourcis Bureau et Menu Démarrer...
powershell -Command "$s=(New-Object -COM WScript.Shell).CreateShortcut('%Public%\Desktop\Ceke''s RAM Test.lnk');$s.TargetPath='%TARGET_DIR%\ram_stress_diag.exe';$s.WorkingDirectory='%TARGET_DIR%';$s.Save()"
powershell -Command "$s=(New-Object -COM WScript.Shell).CreateShortcut('$env:ProgramData\Microsoft\Windows\Start Menu\Programs\Ceke''s RAM Test.lnk');$s.TargetPath='%TARGET_DIR%\ram_stress_diag.exe';$s.WorkingDirectory='%TARGET_DIR%';$s.Save()"

echo.
echo =====================================================================
echo  INSTALLATION REUSSIE !
echo =====================================================================
echo  - Emplacement : %TARGET_DIR%\ram_stress_diag.exe
echo  - Raccourci créé sur le Bureau et le Menu Démarrer.
echo.
pause
