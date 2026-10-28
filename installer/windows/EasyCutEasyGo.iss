; VEDIT: 'Easy Cut, Easy Go!' installer. Kept apart from upstream's Drift.iss so a merge never
; collides with it. Builds from the staged portable folder (C:\dev\vedit-release.ps1):
;   ISCC.exe /DMyAppVersion=0.1.0 /DMyAppSource=<staged folder> EasyCutEasyGo.iss
;
; Installs per user without an admin prompt by default (the wizard offers "all users" too), in
; Korean, with a desktop icon ticked. Never change MyAppId: it is what lets the next installer
; upgrade this install in place.

#ifndef MyAppVersion
  #define MyAppVersion "0.1.0"
#endif
#ifndef MyAppSource
  #define MyAppSource "dist\\bin"
#endif

#define MyAppName "Easy Cut, Easy Go!"
#define MyAppId "6E2B7C51-3F4A-4D8B-9E1C-EC6E0A11C0DE"
#define MyAppPublisher "Easy Cut, Easy Go!"
#define MyAppExeName "ECEG.exe"
#define MyOutputBase "EasyCutEasyGo-Setup-x64"

[Setup]
AppId={{{#MyAppId}}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\EasyCutEasyGo
DefaultGroupName={#MyAppName}
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Most of the size is AI models and DLLs that barely compress; lzma2 at a moderate level keeps
; the build to minutes without giving much back.
Compression=lzma2/normal
SolidCompression=yes
DiskSpanning=no
WizardStyle=modern
SetupIconFile=..\..\resources\windows\drift.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
UninstallDisplayName={#MyAppName}
ChangesAssociations=yes
LicenseFile={#MyAppSource}\LICENSE
OutputDir=output
OutputBaseFilename={#MyOutputBase}

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#MyAppSource}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

; Project files open in the editor. HKA is HKCU for a per-user install, HKLM for all users.
[Registry]
Root: HKA; Subkey: "Software\Classes\.drift"; ValueType: string; ValueName: ""; ValueData: "EasyCutEasyGo.Project"; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\EasyCutEasyGo.Project"; ValueType: string; ValueName: ""; ValueData: "Easy Cut, Easy Go! 프로젝트"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\EasyCutEasyGo.Project\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#MyAppExeName},0"
Root: HKA; Subkey: "Software\Classes\EasyCutEasyGo.Project\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#MyAppExeName}"" ""%1"""

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent
; VEDIT: an in-app update installs silently; the app opens again when it is done.
Filename: "{app}\{#MyAppExeName}"; Flags: nowait; Check: WizardSilent
