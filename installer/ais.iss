; ais.iss -- Inno Setup script for the AIS Windows installer.
;
; A per-user installer (no admin prompt) into %LOCALAPPDATA%\Programs\AIS, where
; VS Code and Ollama install. It puts ais.exe (the command line, with the web
; GUI) and ais-gui.exe (the launcher: the web GUI from a double-click, with a
; notification-area icon; gui/win/ais-gui.c) side by side, as vim/gvim and
; git/git-gui do, and gives the GUI alone a shortcut: one Start-menu entry
; "AIS", a Desktop icon (a ticked task), the folder on the user's PATH for the
; CLI (a ticked task, undone on uninstall). No Start-menu folder and no
; Uninstall shortcut: Settings > Apps is where Windows uninstalls. The index
; lives in the user profile, not here, so uninstalling never touches it.
;
; Built on the Windows runner by release.yml and checked (silent install, the
; shortcuts, silent uninstall) by native-windows.yml. SrcDir is the unzipped
; release folder: ais.exe, ais-gui.exe, COPYING, about.txt, USING.txt.
;   iscc /DSrcDir=<folder> /DAppVersion=<x.y.z> installer\ais.iss

#ifndef SrcDir
  #define SrcDir "."
#endif
#ifndef AppVersion
  #define AppVersion "0.0.0"
#endif

[Setup]
AppId={{BE2750EB-72A2-4016-AFD0-98818CBB51E7}
AppName=AIS
AppVerName=AIS {#AppVersion}
AppVersion={#AppVersion}
AppPublisher=Vasili Gavrilov
AppPublisherURL=https://github.com/Anode1/ais
AppSupportURL=https://github.com/Anode1/ais/issues
DefaultDirName={autopf}\AIS
DisableProgramGroupPage=yes
DisableWelcomePage=no
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline
OutputBaseFilename=ais-{#AppVersion}-windows-x86_64-setup
SetupIconFile={#SourcePath}\..\icons\ais.ico
UninstallDisplayIcon={app}\ais-gui.exe
UninstallDisplayName=AIS
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ChangesEnvironment=yes
LicenseFile={#SrcDir}\COPYING
; ais-gui.exe holds this mutex while it runs: Setup and the uninstaller ask the
; user to Quit it (its tray menu) instead of replacing a running server.
AppMutex=ais-gui
; Restart Manager cannot close a console server; the mutex above is the gate.
CloseApplications=no

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"
Name: "addtopath";   Description: "Add the ais command to my PATH (for use in any terminal)"

[Files]
Source: "{#SrcDir}\ais.exe";          DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\ais-gui.exe";      DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\COPYING";          DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\about.txt";        DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\USING.txt";        DestDir: "{app}"; Flags: ignoreversion
Source: "{#SourcePath}\README.txt";   DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\AIS"; Filename: "{app}\ais-gui.exe"; WorkingDir: "{app}"; Comment: "Open your AIS memory"
Name: "{autodesktop}\AIS";  Filename: "{app}\ais-gui.exe"; WorkingDir: "{app}"; Comment: "Open your AIS memory"; Tasks: desktopicon

[Run]
Filename: "{app}\ais-gui.exe"; Description: "Open AIS now"; Flags: nowait postinstall skipifsilent

[Registry]
Root: HKCU; Subkey: "Environment"; ValueType: expandsz; ValueName: "Path"; \
  ValueData: "{olddata};{app}"; Tasks: addtopath; Check: NeedsAddPath(ExpandConstant('{app}'))

[Code]
function NeedsAddPath(Param: string): Boolean;
var
  Orig: string;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', Orig) then
  begin
    Result := True;
    exit;
  end;
  { already present? compare case-insensitively, bounded by ';' }
  Result := Pos(';' + Uppercase(Param) + ';', ';' + Uppercase(Orig) + ';') = 0;
end;

{ Uninstall: take the install folder back out of the user's PATH, wherever it sits. }
procedure RemoveFromPath(Dir: string);
var
  Orig, Upper: string;
  P: Integer;
begin
  if not RegQueryStringValue(HKCU, 'Environment', 'Path', Orig) then
    exit;
  Upper := ';' + Uppercase(Orig) + ';';
  P := Pos(';' + Uppercase(Dir) + ';', Upper);
  if P = 0 then
    exit;
  { P indexes Upper, which has one ';' prepended: the entry starts at P in Orig }
  Delete(Orig, P, Length(Dir) + 1);
  if (Length(Orig) > 0) and (Orig[1] = ';') then
    Delete(Orig, 1, 1);
  if (Length(Orig) > 0) and (Orig[Length(Orig)] = ';') then
    Delete(Orig, Length(Orig), 1);
  RegWriteExpandStringValue(HKCU, 'Environment', 'Path', Orig);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    RemoveFromPath(ExpandConstant('{app}'));
end;
