; Build through package-installer.ps1 with Inno Setup 6.7 or newer.
#if VER < EncodeVer(6, 7, 0)
  #error Inno Setup 6.7 or newer is required.
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
#ifndef AppImage
  #error AppImage must be supplied by package-installer.ps1.
#endif
#ifndef InstallerBackground
  #error InstallerBackground must be supplied by package-installer.ps1.
#endif
#ifndef ChineseLanguageFile
  #define ChineseLanguageFile "compiler:Languages\ChineseSimplified.isl"
#endif

[Setup]
AppId={{22F6737E-4359-4BF0-948A-7D261B38358C}
AppName=ROCK / 洛克手碟演奏器
AppVersion={#AppVersion}
AppVerName=ROCK / 洛克手碟演奏器 {#AppVersion}
AppPublisher=kanostars
AppPublisherURL=https://github.com/kanostars/Rock-auto-music-player
AppSupportURL=https://github.com/kanostars/Rock-auto-music-player/issues
AppUpdatesURL=https://github.com/kanostars/Rock-auto-music-player/releases
DefaultDirName={localappdata}\Programs\RockAutoMusicPlay
DefaultGroupName=ROCK 洛克手碟演奏器
DisableWelcomePage=no
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
VersionInfoProductName=ROCK / 洛克手碟演奏器
VersionInfoProductVersion={#AppVersion}
VersionInfoProductTextVersion={#AppVersion}
WizardStyle=modern light windows11 excludelightcontrols hidebevels
WizardBackColor=#fffaf3
WizardImageFile=
WizardSmallImageFile={#AppImage}
Compression=lzma2
SolidCompression=yes
ArchiveExtraction=full
CloseApplications=yes
RestartApplications=no
LicenseFile={#PayloadDir}\LICENSE

[Languages]
Name: "chinesesimplified"; MessagesFile: "{#ChineseLanguageFile}"

[Tasks]
Name: "desktopicon"; Description: "创建桌面快捷方式"; GroupDescription: "快捷方式："; Flags: unchecked

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#InstallerBackground}"; DestName: "rock-installer-background.png"; Flags: dontcopy

[Icons]
Name: "{userprograms}\ROCK 洛克手碟演奏器"; Filename: "{app}\RockAutoMusicPlay.exe"; WorkingDir: "{app}"
Name: "{userdesktop}\ROCK 洛克手碟演奏器"; Filename: "{app}\RockAutoMusicPlay.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\RockAutoMusicPlay.exe"; Description: "启动 ROCK / 洛克手碟演奏器"; Flags: nowait postinstall skipifsilent unchecked; Check: CanLaunchApplication

; No recursive UninstallDelete entries: remove only the files installed here.
; Projects, exports, preferences, logs and independently installed drivers remain.
[Code]
const
  ServicesKey = 'SYSTEM\CurrentControlSet\Services';
  InvalidFileAttributes = $FFFFFFFF;
  FileAttributeDirectory = $10;
  DriverDownloadURL = 'https://github.com/oblitum/Interception/releases/download/v1.0.1/Interception.zip';
  DriverArchiveName = 'Interception-v1.0.1.zip';
  DriverArchiveSHA256 = 'ad038963d6413055765128b0b931f6e765147c9916dba79e65d872b261f9af10';
  KeyboardClassKey = 'SYSTEM\CurrentControlSet\Control\Class\{4D36E96B-E325-11CE-BFC1-08002BE10318}';
  MouseClassKey = 'SYSTEM\CurrentControlSet\Control\Class\{4D36E96F-E325-11CE-BFC1-08002BE10318}';

type
  TDriverComponent = (dcPresent, dcMissing, dcUnknown);
  TDriverState = (dsInstalled, dsMissing, dsIncomplete, dsUnknown);

var
  DriverState: TDriverState;
  DriverPage: TWizardPage;
  DriverStatusLabel: TNewStaticText;
  DriverInstallCheck: TNewCheckBox;
  DriverDownloadPage: TDownloadWizardPage;
  DriverProgressPage: TOutputProgressWizardPage;
  DriverPrepared, DriverRestartRequired: Boolean;
  DriverInstallerPath, DriverInstallMessage: String;
  InstallerBackImage: TPngImage;

function GetFileAttributesW(FileName: String): Cardinal;
  external 'GetFileAttributesW@kernel32.dll stdcall';
function ExpandEnvironmentStringsW(Source, Destination: String; Size: Cardinal): Cardinal;
  external 'ExpandEnvironmentStringsW@kernel32.dll stdcall';

procedure UpdateInstallerArtwork(CurPageID: Integer);
var
  Backgrounds: TArrayOfGraphic;
  Opacity: Byte;
begin
  if WizardSilent or HighContrastActive then Exit;
  try
    if InstallerBackImage = nil then
    begin
      ExtractTemporaryFile('rock-installer-background.png');
      InstallerBackImage := TPngImage.Create;
      InstallerBackImage.LoadFromFile(ExpandConstant('{tmp}\rock-installer-background.png'));
    end;
    SetLength(Backgrounds, 1);
    Backgrounds[0] := InstallerBackImage;
    if (CurPageID = wpWelcome) or (CurPageID = wpFinished) then Opacity := 255
    else Opacity := 65;
    WizardSetBackImage(Backgrounds, True, True, Opacity);
  except
    { Decorative artwork must never prevent an installation. }
    Log('Installer artwork unavailable: ' + GetExceptionMessage);
  end;
end;

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
  ServiceKey, DriverPath, RawPath, SystemPath, ClassKey, Filters: String;
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
  begin
    if Service = 'keyboard' then ClassKey := KeyboardClassKey
    else ClassKey := MouseClassKey;
    if not RegGetValueNames(HKLM64, ClassKey, ValueNames) then Exit;
    if not ContainsName(ValueNames, 'UpperFilters') then
      Result := dcMissing
    else if RegQueryMultiStringValue(HKLM64, ClassKey, 'UpperFilters', Filters) then
    begin
      if Pos(#0 + Lowercase(Service) + #0, #0 + Lowercase(Filters) + #0) > 0 then
        Result := dcPresent
      else Result := dcMissing;
    end;
  end;
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

function OfferDriverInstallation: Boolean;
begin
  Result := (DriverState = dsMissing) or (DriverState = dsIncomplete);
end;

function DriverInstallationSelected: Boolean;
begin
  Result := False;
  if WizardSilent then Exit;
  if DriverInstallCheck = nil then Exit;
  Result := DriverInstallCheck.Enabled and DriverInstallCheck.Checked;
end;

function CanLaunchApplication: Boolean;
begin
  Result := not DriverRestartRequired;
end;

procedure RefreshDriverPage;
begin
  case DriverState of
    dsInstalled: DriverStatusLabel.Caption := '已检测到 Interception 驱动组件和过滤器。' + #13#10 +
      '无需重复安装，实际连接状态可在程序的设置页面查看。';
    dsMissing: DriverStatusLabel.Caption := '未检测到 Interception 驱动。' + #13#10 +
      '可勾选下方选项，由安装器下载并安装官方驱动。';
    dsIncomplete: DriverStatusLabel.Caption := 'Interception 驱动组件或过滤器不完整。' + #13#10 +
      '可勾选下方选项，重新安装官方驱动。';
    dsUnknown: DriverStatusLabel.Caption := '暂时无法确认 Interception 驱动状态。' + #13#10 +
      '读取失败不代表未安装，请在程序的设置页面重新检测。';
  end;
  DriverInstallCheck.Enabled := OfferDriverInstallation;
  if not DriverInstallCheck.Enabled then DriverInstallCheck.Checked := False;
end;

function RecheckDriverSelection: Boolean;
begin
  InspectDriver;
  RefreshDriverPage;
  Result := DriverInstallationSelected;
  if not Result then
  begin
    if DriverState = dsInstalled then
      DriverInstallMessage := '已检测到 Interception 驱动，已跳过重复安装。'
    else if DriverState = dsUnknown then
      DriverInstallMessage := '暂时无法确认驱动状态，已跳过驱动安装。可在“设置 → 驱动连接”中重新检测。';
  end;
end;

procedure InitializeWizard;
var
  HelpLabel: TNewStaticText;
begin
  InspectDriver;
  DriverPage := CreateCustomPage(wpSelectTasks, 'Interception 驱动',
    '游戏自动演奏使用此驱动，手碟试听和跟练可以直接使用。');
  DriverStatusLabel := TNewStaticText.Create(DriverPage);
  DriverStatusLabel.Parent := DriverPage.Surface;
  DriverStatusLabel.Left := 0;
  DriverStatusLabel.Top := 0;
  DriverStatusLabel.Width := DriverPage.SurfaceWidth;
  DriverStatusLabel.Height := ScaleY(56);
  DriverStatusLabel.AutoSize := False;
  DriverStatusLabel.WordWrap := True;
  DriverStatusLabel.Font.Style := [fsBold];
  DriverInstallCheck := TNewCheckBox.Create(DriverPage);
  DriverInstallCheck.Parent := DriverPage.Surface;
  DriverInstallCheck.Left := 0;
  DriverInstallCheck.Top := DriverStatusLabel.Height + ScaleY(12);
  DriverInstallCheck.Width := DriverPage.SurfaceWidth;
  DriverInstallCheck.Height := ScaleY(36);
  DriverInstallCheck.Caption := '下载并安装 Interception 驱动（需管理员授权，完成后重启电脑）';
  DriverInstallCheck.Checked := False;
  RefreshDriverPage;
  HelpLabel := TNewStaticText.Create(DriverPage);
  HelpLabel.Parent := DriverPage.Surface;
  HelpLabel.Left := 0;
  HelpLabel.Top := DriverInstallCheck.Top + DriverInstallCheck.Height + ScaleY(12);
  HelpLabel.Width := DriverPage.SurfaceWidth;
  HelpLabel.Height := ScaleY(112);
  HelpLabel.AutoSize := False;
  HelpLabel.WordWrap := True;
  HelpLabel.Caption := '勾选后，安装器会从 oblitum/Interception 官方项目下载 v1.0.1，' +
    '校验文件并请求管理员权限安装，无需打开浏览器或输入命令。' + #13#10 +
    '安装成功后需重启电脑，才能使用游戏自动演奏。' + #13#10 +
    '不勾选也可正常安装程序，手碟试听和歌曲跟练无需此驱动。' + #13#10 +
    '卸载工作台会保留个人工程、导出文件、设置、日志和此驱动。';
  DriverDownloadPage := CreateDownloadPage('下载 Interception 驱动',
    '正在下载并校验官方驱动，请稍候。取消后可返回上一步取消选装。', nil);
  DriverDownloadPage.ShowBaseNameInsteadOfUrl := True;
  DriverProgressPage := CreateOutputProgressPage('安装 Interception 驱动',
    '请在 Windows 提示时允许管理员授权，等待驱动安装完成。');
end;

function PrepareDriverDownload: Boolean;
begin
  Result := False;
  DriverDownloadPage.Clear;
  DriverDownloadPage.Add(DriverDownloadURL, DriverArchiveName, DriverArchiveSHA256);
  DriverDownloadPage.Show;
  try
    try
      DriverDownloadPage.Download;
      ExtractArchive(ExpandConstant('{tmp}\') + DriverArchiveName,
        ExpandConstant('{tmp}\interception-package'), '', True, nil);
      DriverInstallerPath := ExpandConstant('{tmp}\interception-package\Interception\command line installer\install-interception.exe');
      if not FileExists(DriverInstallerPath) then
        RaiseException('官方驱动包中未找到安装程序，请重新下载。');
      DriverPrepared := True;
      Result := True;
    except
      DriverPrepared := False;
      if DriverDownloadPage.AbortedByUser then
        Log('Interception download was cancelled by the user.')
      else
      begin
        Log('Interception download/extraction failed: ' + GetExceptionMessage);
        SuppressibleMsgBox('驱动下载或校验失败：' + GetExceptionMessage + #13#10#13#10 +
          '点击“安装”可重试；也可返回驱动页面取消勾选，仅安装程序。',
          mbError, MB_OK, IDOK);
      end;
    end;
  finally
    DriverDownloadPage.Hide;
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if (CurPageID <> wpReady) or not DriverInstallationSelected then Exit;
  if not RecheckDriverSelection then Exit;
  if not DriverPrepared then Result := PrepareDriverDownload;
end;

function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := MemoDirInfo + NewLine + NewLine + MemoTasksInfo;
  if DriverInstallationSelected then
    Result := Result + NewLine + NewLine + 'Interception 驱动：' + NewLine + Space +
      '从官方项目下载并安装，需管理员授权；安装完成后重启电脑。';
end;

function TryInstallDriver(var Reason: String): Boolean;
var
  ExitCode: Integer;
begin
  Result := False;
  if not ShellExec('runas', DriverInstallerPath, '/install',
    ExtractFileDir(DriverInstallerPath), SW_HIDE, ewWaitUntilTerminated, ExitCode) then
  begin
    if ExitCode = 1223 then Reason := '已取消管理员授权，未安装驱动。'
    else Reason := '无法启动驱动安装程序：' + SysErrorMessage(ExitCode);
    Exit;
  end;
  { Confirm driver registration as well as the process exit code. }
  InspectDriver;
  Result := ((ExitCode = 0) or (ExitCode = 3010)) and (DriverState = dsInstalled);
  if Result then
  begin
    DriverRestartRequired := True;
    DriverInstallMessage := 'Interception 驱动已安装。请重启电脑后再使用自动演奏。'
  end
  else
    Reason := Format('驱动安装未完整完成（退出码 %d）。如已写入部分组件，可重启后在“设置 → 驱动连接”中重新检测。', [ExitCode]);
end;

procedure InstallSelectedDriver;
var
  Success: Boolean;
  Reason: String;
begin
  if not DriverInstallationSelected or not DriverPrepared then Exit;
  if not RecheckDriverSelection then Exit;
  repeat
    Reason := '';
    DriverProgressPage.SetText('正在安装官方驱动，请完成管理员授权。',
      '安装完成后将提示重启电脑。');
    DriverProgressPage.Show;
    try
      try
        Success := TryInstallDriver(Reason);
      except
        Success := False;
        Reason := GetExceptionMessage;
      end;
    finally
      DriverProgressPage.Hide;
    end;
    if Success then Exit;
    Log('Interception installation failed or was skipped: ' + Reason);
    DriverInstallMessage := '程序已安装；Interception 驱动未完成安装。' + #13#10 + Reason;
    if SuppressibleMsgBox(Reason + #13#10#13#10 +
      '“重试”再次安装驱动；“取消”跳过驱动，完成程序安装。',
      mbError, MB_RETRYCANCEL, IDCANCEL) <> IDRETRY then Exit;
  until False;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then InstallSelectedDriver;
end;

function NeedRestart: Boolean;
begin
  Result := DriverRestartRequired;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  UpdateInstallerArtwork(CurPageID);
  if CurPageID = DriverPage.ID then
  begin
    InspectDriver;
    RefreshDriverPage;
  end;
  if CurPageID = wpFinished then
  begin
    if DriverInstallMessage <> '' then
      WizardForm.FinishedLabel.Caption := DriverInstallMessage + #13#10#13#10 +
        WizardForm.FinishedLabel.Caption;
    if DriverRestartRequired then
    begin
      WizardForm.YesRadio.Checked := False;
      WizardForm.NoRadio.Checked := True;
    end;
  end;
end;

procedure DeinitializeSetup;
begin
  if InstallerBackImage <> nil then InstallerBackImage.Free;
end;
