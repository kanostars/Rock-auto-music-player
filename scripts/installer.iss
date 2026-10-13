; Build through package-installer.ps1 with Inno Setup 6.5 or newer.
#if VER < EncodeVer(6, 5, 0)
  #error Inno Setup 6.5 or newer is required.
#endif
#ifndef AppVersion
  #error AppVersion must be supplied by package-installer.ps1.
#endif
#ifndef PayloadDir
  #error PayloadDir must be supplied by package-installer.ps1.
#endif
#ifndef ReleaseDir
  #error ReleaseDir must be supplied by package-installer.ps1.
#endif
#ifndef AppIcon
  #error AppIcon must be supplied by package-installer.ps1.
#endif
#ifndef ChineseLanguageFile
  #define ChineseLanguageFile "compiler:Languages\ChineseSimplified.isl"
#endif

[Setup]
AppId={{22F6737E-4359-4BF0-948A-7D261B38358C}
AppName=ROCK 九键音乐工作台
AppVersion={#AppVersion}
AppVerName=ROCK 九键音乐工作台 {#AppVersion}
AppPublisher=kanostars
AppPublisherURL=https://github.com/kanostars/Rock-auto-music-player
AppSupportURL=https://github.com/kanostars/Rock-auto-music-player/issues
AppUpdatesURL=https://github.com/kanostars/Rock-auto-music-player/releases
DefaultDirName={localappdata}\Programs\RockAutoMusicPlay
DefaultGroupName=ROCK 九键音乐工作台
DisableDirPage=no
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64os
ArchitecturesInstallIn64BitMode=x64os
MinVersion=10.0.17763
OutputDir={#ReleaseDir}
OutputBaseFilename=RockAutoMusicPlay-{#AppVersion}-setup-x64
SetupIconFile={#AppIcon}
UninstallDisplayIcon={app}\RockAutoMusicPlay.exe
VersionInfoVersion={#AppVersion}
VersionInfoProductName=RockAutoMusicPlay
VersionInfoProductVersion={#AppVersion}
VersionInfoProductTextVersion={#AppVersion}
WizardStyle=modern
Compression=lzma2
SolidCompression=yes
CloseApplications=yes
RestartApplications=no
LicenseFile={#PayloadDir}\LICENSE

[Languages]
Name: "chinesesimplified"; MessagesFile: "{#ChineseLanguageFile}"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "快捷方式："; Flags: unchecked

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{userprograms}\ROCK 九键音乐工作台"; Filename: "{app}\RockAutoMusicPlay.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\ROCK 九键音乐工作台"; Filename: "{app}\RockAutoMusicPlay.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\RockAutoMusicPlay.exe"; Description: "启动 ROCK 九键音乐工作台"; Flags: nowait postinstall skipifsilent unchecked
Filename: "https://github.com/oblitum/Interception/releases/download/v1.0.1/Interception.zip"; Description: "在浏览器中下载 Interception 驱动（需另行管理员安装并重启）"; Flags: shellexec postinstall skipifsilent unchecked; Check: OfferDriverDownload

; No recursive UninstallDelete entries: remove only the files installed here.
; Projects, exports, preferences, logs and independently installed drivers remain.
[Code]
const
  ServicesKey = 'SYSTEM\CurrentControlSet\Services';
  InvalidFileAttributes = $FFFFFFFF;
  FileAttributeDirectory = $10;

type
  TDriverComponent = (dcPresent, dcMissing, dcUnknown);
  TDriverState = (dsInstalled, dsMissing, dsIncomplete, dsUnknown);

var
  DriverState: TDriverState;
  DriverPage: TWizardPage;

function GetFileAttributesW(FileName: String): Cardinal;
  external 'GetFileAttributesW@kernel32.dll stdcall';
function ExpandEnvironmentStringsW(Source, Destination: String; Size: Cardinal): Cardinal;
  external 'ExpandEnvironmentStringsW@kernel32.dll stdcall';

function ContainsName(const Names: TArrayOfString; const Name: String): Boolean;
var
  I: Integer;
begin
  Result := False;
  for I := 0 to GetArrayLength(Names) - 1 do
    if CompareText(Names[I], Name) = 0 then
    begin
      Result := True;
      Exit;
    end;
end;

function ResolveDriverPath(const RawPath: String; var DriverPath: String): Boolean;
var
  Size: Cardinal;
begin
  DriverPath := StringOfChar(#0, 32768);
  Size := ExpandEnvironmentStringsW(Trim(RawPath), DriverPath, Length(DriverPath));
  Result := (Size > 1) and (Size <= Cardinal(Length(DriverPath)));
  if not Result then Exit;
  SetLength(DriverPath, Size - 1);
  DriverPath := RemoveQuotes(Trim(DriverPath));
  if CompareText(Copy(DriverPath, 1, 12), '\SystemRoot\') = 0 then
    DriverPath := ExpandConstant('{win}\') + Copy(DriverPath, 13, Length(DriverPath))
  else if CompareText(Copy(DriverPath, 1, 4), '\??\') = 0 then
    Delete(DriverPath, 1, 4)
  else if CompareText(Copy(DriverPath, 1, 9), 'System32\') = 0 then
    DriverPath := ExpandConstant('{win}\') + DriverPath;
  Result := (Length(DriverPath) > 2) and (Copy(DriverPath, 2, 2) = ':\') and
    (Pos('%', DriverPath) = 0) and (CompareText(ExtractFileExt(DriverPath), '.sys') = 0);
end;

function InspectDriverComponent(const Service: String; const ServiceNames: TArrayOfString;
  var HasService: Boolean): TDriverComponent;
var
  ServiceKey, DriverPath, RawPath, SystemPath: String;
  ValueNames: TArrayOfString;
  ServiceType, Attributes: Cardinal;
  ErrorCode: LongInt;
begin
  Result := dcUnknown;
  HasService := ContainsName(ServiceNames, Service);
  if not HasService then
  begin
    Result := dcMissing;
    Exit;
  end;
  ServiceKey := ServicesKey + '\' + Service;
  if not RegQueryDWordValue(HKLM64, ServiceKey, 'Type', ServiceType) then Exit;
  if ServiceType <> 1 then Exit;
  if not RegGetValueNames(HKLM64, ServiceKey, ValueNames) then Exit;
  if ContainsName(ValueNames, 'ImagePath') then
  begin
    if not RegQueryStringValue(HKLM64, ServiceKey, 'ImagePath', RawPath) then Exit;
    if not ResolveDriverPath(RawPath, DriverPath) then Exit;
  end
  else
    { The official driver installer may omit ImagePath; use its standard SYS name. }
    DriverPath := ExpandConstant('{sys}\drivers\') + Service + '.sys';

  { The script builds a 32-bit Setup for an x64 app. Read native SYS files via Sysnative. }
  SystemPath := ExpandConstant('{win}\System32\');
  if CompareText(Copy(DriverPath, 1, Length(SystemPath)), SystemPath) = 0 then
    DriverPath := ExpandConstant('{sysnative}\') +
      Copy(DriverPath, Length(SystemPath) + 1, Length(DriverPath));
  Attributes := GetFileAttributesW(DriverPath);
  if Attributes = InvalidFileAttributes then
  begin
    ErrorCode := DLLGetLastError;
    if (ErrorCode = 2) or (ErrorCode = 3) then Result := dcMissing;
    Log(Format('Interception %s: file inspection returned %d.', [Service, ErrorCode]));
  end
  else if (Attributes and FileAttributeDirectory) = 0 then
    Result := dcPresent;
end;

procedure InspectDriver;
var
  ServiceNames: TArrayOfString;
  Keyboard, Mouse: TDriverComponent;
  HasKeyboard, HasMouse: Boolean;
begin
  DriverState := dsUnknown;
  { Successful enumeration distinguishes absent services from denied registry access. }
  if not RegGetSubkeyNames(HKLM64, ServicesKey, ServiceNames) then Exit;
  Keyboard := InspectDriverComponent('keyboard', ServiceNames, HasKeyboard);
  Mouse := InspectDriverComponent('mouse', ServiceNames, HasMouse);
  if (Keyboard = dcUnknown) or (Mouse = dcUnknown) then Exit;
  if (Keyboard = dcPresent) and (Mouse = dcPresent) then DriverState := dsInstalled
  else if (Keyboard = dcMissing) and (Mouse = dcMissing) and
    not HasKeyboard and not HasMouse then DriverState := dsMissing
  else DriverState := dsIncomplete;
  Log(Format('Interception installation inspection: %d.', [Ord(DriverState)]));
end;

function OfferDriverDownload: Boolean;
begin
  Result := (DriverState = dsMissing) or (DriverState = dsIncomplete);
end;

procedure InitializeWizard;
var
  StatusLabel, HelpLabel: TNewStaticText;
begin
  InspectDriver;
  DriverPage := CreateCustomPage(wpSelectTasks, 'Interception 驱动',
    '游戏自动演奏使用此驱动，手碟试听和跟练可以直接使用。');
  StatusLabel := TNewStaticText.Create(DriverPage);
  StatusLabel.Parent := DriverPage.Surface;
  StatusLabel.Left := 0;
  StatusLabel.Top := 0;
  StatusLabel.Width := DriverPage.SurfaceWidth;
  StatusLabel.Height := ScaleY(56);
  StatusLabel.AutoSize := False;
  StatusLabel.WordWrap := True;
  StatusLabel.Font.Style := [fsBold];
  case DriverState of
    dsInstalled: StatusLabel.Caption := '已检测到 Interception 驱动组件。' + #13#10 +
      '实际连接状态可在程序的设置页面查看。';
    dsMissing: StatusLabel.Caption := '未检测到 Interception 驱动。' + #13#10 +
      '安装完成后，可以选择在浏览器中下载官方驱动。';
    dsIncomplete: StatusLabel.Caption := 'Interception 驱动组件不完整。' + #13#10 +
      '安装完成后，可以选择下载官方驱动进行修复。';
    dsUnknown: StatusLabel.Caption := '暂时无法确认 Interception 驱动状态。' + #13#10 +
      '读取失败不代表驱动未安装，请在程序的设置页面重新检测。';
  end;
  HelpLabel := TNewStaticText.Create(DriverPage);
  HelpLabel.Parent := DriverPage.Surface;
  HelpLabel.Left := 0;
  HelpLabel.Top := StatusLabel.Top + StatusLabel.Height + ScaleY(16);
  HelpLabel.Width := DriverPage.SurfaceWidth;
  HelpLabel.Height := ScaleY(112);
  HelpLabel.AutoSize := False;
  HelpLabel.WordWrap := True;
  HelpLabel.Caption := '驱动来自官方项目 oblitum/Interception。' + #13#10 +
    '下载后，需在管理员命令提示符中运行：' + #13#10 +
    'install-interception.exe /install' + #13#10 +
    '然后重启电脑。驱动是否能正常连接以设置页面的检测结果为准。' + #13#10 +
    '卸载工作台会保留个人工程、导出文件、设置、日志和此驱动。';
end;
