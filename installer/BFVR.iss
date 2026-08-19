#include "..\BFVRVersion.inc"
#define AppName "BFVR"
#define AppVersion BFVR_VERSION_STRING
#ifndef PayloadRoot
  #define PayloadRoot AddBackslash(SourcePath) + "..\..\build\bfvr-installer-payload-v" + AppVersion + "-test\BFVR"
#endif
#ifndef OutputRoot
  #define OutputRoot AddBackslash(SourcePath) + "..\..\build\bfvr-release-output"
#endif

[Setup]
AppId={{1C2646DE-4BFD-4491-B102-7A40AFB0953F}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} v{#AppVersion}
VersionInfoVersion={#AppVersion}.0
VersionInfoDescription=BFVR v{#AppVersion} Installer
DefaultDirName={code:GetDefaultBfvrDirectory}
DefaultGroupName=BFVR
OutputDir={#OutputRoot}
OutputBaseFilename=BFVR-Setup-v{#AppVersion}
SetupIconFile=..\assets\BFVR.ico
UninstallDisplayIcon={app}\BFVR.exe
ArchitecturesAllowed=x64compatible
MinVersion=10.0.17763
PrivilegesRequired=admin
PrivilegesRequiredOverridesAllowed=commandline
WizardStyle=modern
Compression=lzma2/max
SolidCompression=yes
CloseApplications=force
RestartApplications=no
AllowNoIcons=yes
UsePreviousAppDir=yes
DirExistsWarning=no
DisableProgramGroupPage=auto
SignedUninstaller=no

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Shortcuts:"; Flags: unchecked

[Files]
Source: "{#PayloadRoot}\BFVR.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadRoot}\BFVRClient.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadRoot}\BFVRD3D8To9.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadRoot}\BFVRPresenter.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadRoot}\UserConfig.txt"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#PayloadRoot}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadRoot}\docs\INSTALLATION.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#PayloadRoot}\docs\USER_GUIDE.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#PayloadRoot}\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#PayloadRoot}\assets\*"; DestDir: "{app}\assets"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#PayloadRoot}\runtime\openxr\win64\openxr_loader.dll"; DestDir: "{app}\runtime\openxr\win64"; Flags: ignoreversion
Source: "{#PayloadRoot}\licenses\*"; DestDir: "{app}\licenses"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\BFVR"; Filename: "{app}\BFVR.exe"; WorkingDir: "{app}"
Name: "{group}\BFVR User Guide"; Filename: "{app}\docs\USER_GUIDE.md"
Name: "{autodesktop}\BFVR"; Filename: "{app}\BFVR.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\BFVR.exe"; Description: "Start BFVR"; WorkingDir: "{app}"; Flags: postinstall nowait skipifsilent unchecked

[UninstallDelete]
Type: files; Name: "{app}\loader.log"
Type: files; Name: "{app}\BFVRPresenter-*.log"
Type: filesandordirs; Name: "{app}\logs"

[Code]
function IsGameRoot(const Candidate: String): Boolean;
begin
  Result := FileExists(AddBackslash(Candidate) + 'BF1942.exe');
end;

procedure ShowBf42PlusPlusWarning(const BundledProxyPresent: Boolean);
var
  ErrorCode: Integer;
  Detail: String;
begin
  if BundledProxyPresent then
    Detail :=
      'Setup found dsound.dll. BFVR will inspect it when launched and will use it directly only if it is structurally recognized as a BF42++ proxy.'
  else
    Detail :=
      'Setup did not find bf42++.dll or dsound.dll. Installation may continue, but BFVR cannot start until compatible BF42++ files are available.';
  if MsgBox(
       Detail + #13#10 + #13#10 +
       'BF42++ is a separate project and is not included with BFVR. Do not add a second copy if this game package already includes BF42++ as dsound.dll.' + #13#10 + #13#10 +
       'Open the official BF42++ download page now?',
       mbInformation,
       MB_YESNO) = IDYES then
    ShellExec(
      '',
      'https://www.moddb.com/games/battlefield-1942/addons/bf42plusplus-v2-0-rc6',
      '',
      '',
      SW_SHOWNORMAL,
      ewNoWait,
      ErrorCode);
end;

function FindGameRoot(): String;
var
  Candidate: String;
begin
  Candidate := ExpandConstant('{src}');
  if IsGameRoot(Candidate) then
  begin
    Result := Candidate;
    exit;
  end;

  if RegQueryStringValue(HKLM32, 'SOFTWARE\EA GAMES\Battlefield 1942', 'GAMEDIR', Candidate) and IsGameRoot(Candidate) then
  begin
    Result := Candidate;
    exit;
  end;
  if RegQueryStringValue(HKLM32, 'SOFTWARE\EA GAMES\Battlefield 1942', 'Install Dir', Candidate) and IsGameRoot(Candidate) then
  begin
    Result := Candidate;
    exit;
  end;

  Candidate := ExpandConstant('{pf32}\EA GAMES\Battlefield 1942');
  Result := Candidate;
end;

function GetDefaultBfvrDirectory(Param: String): String;
begin
  Result := AddBackslash(FindGameRoot()) + 'BFVR';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  SelectedDirectory: String;
  GameRoot: String;
  GameExecutable: String;
begin
  Result := True;
  if CurPageID <> wpSelectDir then
    exit;

  SelectedDirectory := RemoveBackslashUnlessRoot(WizardDirValue());
  if IsGameRoot(SelectedDirectory) then
  begin
    SelectedDirectory := AddBackslash(SelectedDirectory) + 'BFVR';
    WizardForm.DirEdit.Text := SelectedDirectory;
  end;

  if CompareText(ExtractFileName(SelectedDirectory), 'BFVR') <> 0 then
  begin
    MsgBox('Select the BFVR folder inside your Battlefield 1942 folder. The destination must end with "BFVR".', mbError, MB_OK);
    Result := False;
    exit;
  end;

  GameRoot := ExtractFileDir(SelectedDirectory);
  GameExecutable := AddBackslash(GameRoot) + 'BF1942.exe';
  if not FileExists(GameExecutable) then
  begin
    MsgBox('BF1942.exe was not found in the parent folder. Select your Battlefield 1942 folder and let Setup add the BFVR subfolder.', mbError, MB_OK);
    Result := False;
    exit;
  end;

  if not FileExists(AddBackslash(GameRoot) + 'bf42++.dll') then
    ShowBf42PlusPlusWarning(
      FileExists(AddBackslash(GameRoot) + 'dsound.dll'));

end;

procedure RestoreIntroMovieIfNeeded();
var
  GameRoot: String;
  IntroMovie: String;
  DisabledIntroMovie: String;
begin
  GameRoot := ExtractFileDir(ExpandConstant('{app}'));
  IntroMovie := AddBackslash(GameRoot) + 'Movies\Intro.bik';
  DisabledIntroMovie := IntroMovie + '.bfvr-disabled';
  if (not FileExists(IntroMovie)) and FileExists(DisabledIntroMovie) then
    RenameFile(DisabledIntroMovie, IntroMovie);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  CommandLine: String;
  SilentUninstall: Boolean;
begin
  if CurUninstallStep = usUninstall then
  begin
    RestoreIntroMovieIfNeeded();
    CommandLine := Uppercase(GetCmdTail());
    SilentUninstall :=
      (Pos('/SILENT', CommandLine) > 0) or
      (Pos('/VERYSILENT', CommandLine) > 0);
    if SilentUninstall or
       (MsgBox('Remove your saved BFVR settings too?', mbConfirmation, MB_YESNO) = IDYES) then
      DeleteFile(ExpandConstant('{app}\UserConfig.txt'));
  end;
end;
