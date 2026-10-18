param(
    [string]$IsccPath,
    [string]$PackageDir,
    [string]$Version,
    [string]$LanguageFile,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$Version) {
    $cmakeText = Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw
    if ($cmakeText -notmatch 'project\(RockAutoMusicPlay VERSION (\d+\.\d+\.\d+)') {
        throw 'Cannot determine application version.'
    }
    $Version = $Matches[1]
}
if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw 'Version must use major.minor.patch format.' }

if (!$IsccPath) {
    $compilerCommand = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($compilerCommand) { $IsccPath = $compilerCommand.Source }
}
if (!$IsccPath -or !(Test-Path -LiteralPath $IsccPath -PathType Leaf)) {
    throw 'Provide -IsccPath pointing to the Inno Setup compiler (ISCC.exe, version 6.5 or newer).'
}
$IsccPath = (Resolve-Path -LiteralPath $IsccPath).Path
# installer.iss checks the real compiler version via ISPP VER, not EXE metadata.
if (!$LanguageFile) {
    $LanguageFile = Join-Path (Split-Path -Parent $IsccPath) 'Languages/ChineseSimplified.isl'
}
if (!(Test-Path -LiteralPath $LanguageFile -PathType Leaf)) {
    throw 'Provide -LanguageFile pointing to ChineseSimplified.isl from the official jrsoftware/issrc Files/Languages directory.'
}
$LanguageFile = (Resolve-Path -LiteralPath $LanguageFile).Path

$releaseRoot = Join-Path $projectRoot 'artifacts/releases'
$installerName = "RockAutoMusicPlay-$Version-setup-x64.exe"
$installerPath = Join-Path $releaseRoot $installerName
$checksumPath = "$installerPath.sha256"
if ((Test-Path -LiteralPath $installerPath) -or (Test-Path -LiteralPath $checksumPath)) {
    throw "Installer output already exists: $installerPath. Preserve or move it before packaging again."
}
if (!$PackageDir) {
    $PackageDir = Join-Path $releaseRoot "RockAutoMusicPlay-$Version-windows-x64"
    if (!(Test-Path -LiteralPath $PackageDir -PathType Container)) {
        $cmakeText = Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw
        if ($cmakeText -notmatch "project\(RockAutoMusicPlay VERSION $([regex]::Escape($Version))\b") {
            throw 'A non-current version requires -PackageDir with its existing release payload.'
        }
        & (Join-Path $PSScriptRoot 'package-release.ps1') -SkipBuild:$SkipBuild
    }
}
$PackageDir = (Resolve-Path -LiteralPath $PackageDir).Path
$packageExe = Join-Path $PackageDir 'RockAutoMusicPlay.exe'
if (!(Test-Path -LiteralPath $packageExe -PathType Leaf)) { throw 'Release payload is missing RockAutoMusicPlay.exe.' }
if ((Get-Item -LiteralPath $packageExe).VersionInfo.ProductVersion -ne $Version) {
    throw 'Packaged executable version does not match the requested installer version.'
}
foreach ($required in @('interception.dll', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
    'libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll', 'platforms/qwindows.dll',
    'imageformats/qgif.dll', 'imageformats/qjpeg.dll', 'imageformats/qico.dll',
    'README.md', 'LICENSE', 'samples/studio-demo.mid', 'licenses/SOURCES.md', 'licenses/Interception.txt',
    'licenses/LGPL-3.0-only.txt', 'licenses/GCC-exception-3.1.txt', 'SHA256SUMS.txt')) {
    if (!(Test-Path -LiteralPath (Join-Path $PackageDir $required) -PathType Leaf)) {
        throw "Incomplete release payload: $required. Run package-release.ps1 first."
    }
}
New-Item -ItemType Directory -Path $releaseRoot -Force | Out-Null
$compilerArguments = @(
    "/DAppVersion=$Version",
    "/DPayloadDir=$PackageDir",
    "/DReleaseDir=$releaseRoot",
    "/DAppIcon=$(Join-Path $projectRoot 'assets/icons/app.ico')",
    "/DChineseLanguageFile=$LanguageFile",
    (Join-Path $PSScriptRoot 'installer.iss')
)
& $IsccPath @compilerArguments
if ($LASTEXITCODE) { throw "Inno Setup compilation failed with exit code $LASTEXITCODE." }
if (!(Test-Path -LiteralPath $installerPath -PathType Leaf)) { throw 'Compiler did not produce the expected installer.' }
if ((Get-Item -LiteralPath $installerPath).VersionInfo.ProductVersion.Trim() -ne $Version) {
    throw 'Installer product version does not match the requested version.'
}
$installerHash = (Get-FileHash -LiteralPath $installerPath -Algorithm SHA256).Hash.ToLowerInvariant()
"$installerHash  $installerName" | Set-Content -LiteralPath $checksumPath -Encoding ascii
Write-Output "Installer: $installerPath"
Write-Output "SHA256: $installerHash"
