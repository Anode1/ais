; ais.iss -- Inno Setup script for the AIS Windows installer.
;
; Produces a per-user installer .exe (no admin prompt): it installs the Cygwin-built
; ais.exe + cygwin1.dll + launcher + docs, adds a Start-Menu shortcut that opens
; the web GUI, and offers to put `ais` on PATH for the command line. Your memory
; (the index) lives in your user profile, NOT in the install dir, so
; uninstalling never touches your data.
;
; Build (on Windows, Inno Setup 6+); SrcDir must hold ais.exe, cygwin1.dll,
; ais-start.bat, ais.ico, README.txt, COPYING, THIRD-PARTY-NOTICES.txt,
; about.txt, USING.txt, ais.tcl:
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
Source: "{#SrcDir}\cygwin1.dll";   DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\ais-start.bat"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\ais.ico";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\ais.tcl";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\README.txt";    DestDir: "{app}"; Flags: ignoreversion isreadme
Source: "{#SrcDir}\COPYING";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\THIRD-PARTY-NOTICES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\about.txt";     DestDir: "{app}"; Flags: ignoreversion
Source: "{#SrcDir}\USING.txt";     DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\AIS (web GUI)"; Filename: "{app}\ais-start.bat"; WorkingDir: "{app}"; IconFilename: "{app}\ais.ico"; Comment: "Open your AIS memory in the browser"
Name: "{group}\Uninstall AIS"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\ais-start.bat"; Description: "Open AIS now"; Flags: nowait postinstall skipifsilent

[Registry]
; Append the install dir to the per-user PATH when the task is chosen.
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
