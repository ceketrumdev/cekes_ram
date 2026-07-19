; Script Inno Setup pour Ceke's RAM Test v1.1

[Setup]
AppName=Ceke's RAM Test
AppVersion=1.1.0
WizardStyle=modern
DefaultDirName={autopf}\Ceke's RAM Test
DefaultGroupName=Ceke's RAM Test
Compression=lzma2
SolidCompression=yes
OutputBaseFilename=Setup_Cekes_RAM_Test_v1.1
ArchitecturesInstallIn64BitMode=x64
PrivilegesRequired=admin

[Files]
Source: "bin\ram_stress_diag.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "driver\cekes_ram_drv.sys"; DestDir: "{app}"; Flags: ignoreversion skipifnottemplate
Source: "README.md"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\Ceke's RAM Test"; Filename: "{app}\ram_stress_diag.exe"
Name: "{autodesktop}\Ceke's RAM Test"; Filename: "{app}\ram_stress_diag.exe"

[Run]
Filename: "sc.exe"; Parameters: "create CekesRamDriver type= kernel binPath= ""{app}\cekes_ram_drv.sys"" start= auto"; Flags: runhidden; StatusMsg: "Enregistrement du service Pilote Kernel..."
Filename: "sc.exe"; Parameters: "start CekesRamDriver"; Flags: runhidden; StatusMsg: "Démarrage du Pilote Kernel..."
Filename: "{app}\ram_stress_diag.exe"; Description: "Lancer Ceke's RAM Test maintenant"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "sc.exe"; Parameters: "stop CekesRamDriver"; Flags: runhidden
Filename: "sc.exe"; Parameters: "delete CekesRamDriver"; Flags: runhidden
