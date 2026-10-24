; ais.iss -- Inno Setup script for the AIS Windows installer (native build).
;
; Produces a per-user installer .exe (no admin prompt). It installs the
; Cygwin-free ais.exe (the command line, with the web GUI) + the ais-web.bat
; launcher + docs, adds a Start-Menu shortcut that opens the web GUI, and offers
; to put `ais` on PATH for terminal use. Your memory (the index) lives in your
; user profile, NOT in the install dir, so uninstalling never touches your data.
;
; Install location: per-user, %LOCALAPPDATA%\Programs\AIS (the same place VS Code
; and other modern per-user apps install), no admin/UAC prompt.
;
; Build (on Windows, Inno Setup 6+); SrcDir must hold ais.exe, ais-web.bat,
; ais.ico, README.txt, COPYING, about.txt, USING.txt:
;   ISCC /DSrcDir=<bundle dir> /DAppVersion=<x.y.z> installer\ais.iss

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
DefaultDirName={autopf}\AIS
DefaultGroupName=AIS
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
OutputBaseFilename=ais-{#AppVersion}-windows-x86_64-installer
SetupIconFile={#SrcDir}\ais.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ChangesEnvironment=yes
LicenseFile={#SrcDir}\COPYING

[Tasks]
Name: "addtopath"; Description: "Add ais to my PATH (use the 'ais' command in any terminal)"; Flags: checkedonce

[Files]
Source: "{#SrcDir}\ais.exe";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\ais-web.bat";   DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\ais.ico";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\README.txt";    DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\COPYING";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\about.txt";     DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\USING.txt";     DestDir: "{app}"; Flags: ignoreversion

[InstallDelete]
; Upgrading over a pre-native (Cygwin) install: remove the now-unused leftovers
; so the folder doesn't accumulate dead files. Runs before the new files copy.
Type: files; Name: "{app}\cygwin1.dll"
Type: files; Name: "{app}\ais-start.bat"
Type: files; Name: "{app}\ais.tcl"
Type: files; Name: "{app}\THIRD-PARTY-NOTICES.txt"

[Icons]
; The web GUI through its launcher: the console it keeps open is the server,
; closed to stop it, as on every platform. The command-line ais.exe gets no
; shortcut of its own: it lives on PATH for terminal use.
Name: "{group}\AIS"; Filename: "{app}\ais-web.bat"; WorkingDir: "{app}"; IconFilename: "{app}\ais.ico"; Comment: "Open your AIS memory"
Name: "{group}\Uninstall AIS"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\ais-web.bat"; Description: "Open AIS now"; Flags: nowait postinstall skipifsilent

[Registry]
; Append the install dir to the per-user PATH when the task is chosen, so `ais`
; works in any terminal.
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
