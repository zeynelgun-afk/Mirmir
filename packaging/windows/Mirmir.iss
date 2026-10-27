#ifndef AppVersion
  #error AppVersion must match the cooked payload release
#endif
#ifndef PayloadRoot
  #error PayloadRoot must point to a verified Windows Shipping payload
#endif
#ifndef OutputRoot
  #define OutputRoot "."
#endif
[Setup]
AppId={{E7EB7B13-04FA-4956-B334-85E5409CF8CB}
AppName=Mirmir
AppVersion={#AppVersion}
AppPublisher=Mirmir contributors
AppPublisherURL=https://github.com/zeynelgun-afk/Mirmir
DefaultDirName={localappdata}\Programs\Mirmir
DefaultGroupName=Mirmir
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DisableProgramGroupPage=yes
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
OutputDir={#OutputRoot}
OutputBaseFilename=Mirmir-{#AppVersion}-windows-x64-setup
UninstallDisplayIcon={app}\MirmirShell.exe
CloseApplications=yes
CloseApplicationsFilter=MirmirShell.exe
RestartApplications=no
[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "turkish"; MessagesFile: "compiler:Languages\Turkish.isl"
[Tasks]
Name: desktopicon; Description: "{cm:DesktopShortcut}"; Flags: unchecked
[CustomMessages]
english.DesktopShortcut=Create a desktop shortcut
turkish.DesktopShortcut=Masaüstü kısayolu oluştur
english.StartMirmir=Start Mirmir
turkish.StartMirmir=Mırmır'ı başlat
[Files]
Source: "{#PayloadRoot}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{group}\Mirmir"; Filename: "{app}\MirmirShell.exe"
Name: "{group}\Stop Mirmir"; Filename: "{app}\MirmirShell.exe"; Parameters: "--stop"
Name: "{autodesktop}\Mirmir"; Filename: "{app}\MirmirShell.exe"; Tasks: desktopicon
[Run]
Filename: "{app}\MirmirShell.exe"; Description: "{cm:StartMirmir}"; Flags: nowait postinstall skipifsilent
; Cat saves are deliberately not listed in UninstallDelete: upgrades/uninstall
; preserve the user's LOCALAPPDATA data. No scheduled task or autostart key.
