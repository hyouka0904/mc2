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
; Traditional Chinese is not part of every Inno Setup installation
#if FileExists(AddBackslash(CompilerPath) + "Languages\ChineseTraditional.isl")
Name: "zh_tw"; MessagesFile: "compiler:Languages\ChineseTraditional.isl"
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "firewall"; Description: "Allow LAN multiplayer through Windows Firewall (mc2.exe, UDP; asks for administrator rights once)"; GroupDescription: "Network:"

[Files]
Source: "{#SourceDir}\*"; DestDir: "{app}"; Flags: recursesubdirs createallsubdirs ignoreversion

[Icons]
Name: "{group}\MechCommander 2"; Filename: "{app}\mc2.exe"; WorkingDir: "{app}"
Name: "{group}\{cm:UninstallProgram,MechCommander 2}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\MechCommander 2"; Filename: "{app}\mc2.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\mc2.exe"; Description: "{cm:LaunchProgram,MechCommander 2}"; WorkingDir: "{app}"; Flags: nowait postinstall skipifsilent

[Code]
// Windows Firewall rule for LAN multiplayer: hosting a game means accepting
// UDP from other players (port 27015). The rule is tied to mc2.exe and UDP,
// on every network profile (Windows 10/11 often mark a new home network as
// Public). Adding a rule needs administrator rights, so a per-user install
// raises one UAC prompt; declining it just leaves the rule out (Windows then
// asks the first time a game is hosted). A marker file records that the rule
// was added, so uninstall removes it only then.
const
  FirewallRule = 'MechCommander2-LAN';
  FirewallMarker = 'firewall_rule_added.txt';

function RunNetsh(const Commands: String): Boolean;
var
  ResultCode: Integer;
begin
  // One elevated cmd.exe for all netsh calls: a single UAC prompt.
  Result := ShellExec('runas', ExpandConstant('{cmd}'), '/c "' + Commands + '"', '',
    SW_HIDE, ewWaitUntilTerminated, ResultCode) and (ResultCode = 0);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Exe: String;
begin
  if (CurStep = ssPostInstall) and WizardIsTaskSelected('firewall') then
  begin
    Exe := ExpandConstant('{app}\mc2.exe');
    if RunNetsh('netsh advfirewall firewall delete rule name=' + FirewallRule + ' >nul 2>&1 & ' +
        'netsh advfirewall firewall add rule name=' + FirewallRule +
        ' dir=in action=allow protocol=UDP program="' + Exe + '" profile=any enable=yes') then
      SaveStringToFile(ExpandConstant('{app}\' + FirewallMarker), FirewallRule, False)
    else
      Log('Firewall rule was not added (UAC declined or netsh failed)');
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if (CurUninstallStep = usUninstall) and FileExists(ExpandConstant('{app}\' + FirewallMarker)) then
  begin
    RunNetsh('netsh advfirewall firewall delete rule name=' + FirewallRule);
    DeleteFile(ExpandConstant('{app}\' + FirewallMarker));
  end;
end;
