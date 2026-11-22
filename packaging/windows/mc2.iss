; SPDX-License-Identifier: GPL-3.0-only
; Inno Setup script for the Windows installer. scripts/package.py runs it:
;   ISCC /DAppVersion=1.2.3 /DSourceDir=<staged game> /DOutputDir=<dist>
;        /DOutputName=<file name> /DIconFile=<icon.ico> mc2.iss
; Per-user install by default (no administrator rights needed); the setup
; offers an all-users install in Program Files as well. Saved games live in
; %APPDATA%\MechCommander2 and are kept on uninstall.

#ifndef AppVersion
  #error AppVersion is not defined
#endif

[Setup]
AppId={{6F0E8C52-3B7A-4E2B-9C1D-5A7F2E4B8D31}
AppName=MechCommander 2
AppVersion={#AppVersion}
AppVerName=MechCommander 2 {#AppVersion}
AppPublisher=MechCommander 2 port contributors (unofficial)
DefaultDirName={autopf}\MechCommander 2
DefaultGroupName=MechCommander 2
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir={#OutputDir}
OutputBaseFilename={#OutputName}
SetupIconFile={#IconFile}
UninstallDisplayIcon={app}\mc2.exe
LicenseFile={#SourceDir}\licenses\EULA.txt
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern

[Languages]
Name: "en"; MessagesFile: "compiler:Default.isl"
Name: "zh_tw"; MessagesFile: "compiler:Languages\ChineseTraditional.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\MechCommander 2"; Filename: "{app}\mc2.exe"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallProgram,MechCommander 2}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\MechCommander 2"; Filename: "{app}\mc2.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\mc2.exe"; Description: "{cm:LaunchProgram,MechCommander 2}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent
