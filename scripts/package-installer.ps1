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
    throw 'Provide -IsccPath pointing to the Inno Setup compiler (ISCC.exe, version 6.7 or newer).'
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
$artworkSource = Join-Path $projectRoot 'assets/installer/wallpaper.png'
$appImage = Join-Path $projectRoot 'assets/icons/app.png'
foreach ($imageFile in @($artworkSource, $appImage)) {
    if (!(Test-Path -LiteralPath $imageFile -PathType Leaf)) { throw "Missing installer artwork: $imageFile" }
}
# Preserve the supplied illustration and prepare a DPI-independent 497:360 wizard canvas.
# The left-biased crop retains the whole character; the text area fades into warm white.
Add-Type -AssemblyName System.Drawing
$artworkDir = Join-Path $releaseRoot 'installer-artwork'
New-Item -ItemType Directory -Path $artworkDir -Force | Out-Null
$wizardImage = Join-Path $artworkDir 'wizard-background.png'
$sourceImage = [System.Drawing.Image]::FromFile($artworkSource)
$canvas = $null
$graphics = $null
$fade = $null
try {
    $canvas = [System.Drawing.Bitmap]::new(1491, 1080, [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $graphics = [System.Drawing.Graphics]::FromImage($canvas)
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $cropWidth = [Math]::Min($sourceImage.Width, $sourceImage.Height * 1491.0 / 1080.0)
    $cropHeight = $cropWidth * 1080.0 / 1491.0
    $sourceRect = [System.Drawing.RectangleF]::new(
        ($sourceImage.Width - $cropWidth) * 0.4, ($sourceImage.Height - $cropHeight) / 2,
        $cropWidth, $cropHeight)
    $canvasRect = [System.Drawing.RectangleF]::new(0, 0, $canvas.Width, $canvas.Height)
    $graphics.DrawImage($sourceImage, $canvasRect, $sourceRect, [System.Drawing.GraphicsUnit]::Pixel)
    $fade = [System.Drawing.Drawing2D.LinearGradientBrush]::new($canvasRect,
        [System.Drawing.Color]::Transparent, [System.Drawing.Color]::FromArgb(246, 255, 250, 243),
        [System.Drawing.Drawing2D.LinearGradientMode]::Horizontal)
    $blend = [System.Drawing.Drawing2D.ColorBlend]::new(4)
    $blend.Positions = [single[]]@(0, 0.32, 0.48, 1)
    $blend.Colors = [System.Drawing.Color[]]@(
        [System.Drawing.Color]::FromArgb(0, 255, 250, 243),
        [System.Drawing.Color]::FromArgb(0, 255, 250, 243),
        [System.Drawing.Color]::FromArgb(246, 255, 250, 243),
        [System.Drawing.Color]::FromArgb(246, 255, 250, 243))
    $fade.InterpolationColors = $blend
    $graphics.FillRectangle($fade, $canvasRect)
    $canvas.Save($wizardImage, [System.Drawing.Imaging.ImageFormat]::Png)
} finally {
    if ($fade) { $fade.Dispose() }
    if ($graphics) { $graphics.Dispose() }
    if ($canvas) { $canvas.Dispose() }
    $sourceImage.Dispose()
}
$compilerArguments = @(
    "/DAppVersion=$Version",
    "/DPayloadDir=$PackageDir",
    "/DReleaseDir=$releaseRoot",
    "/DAppIcon=$(Join-Path $projectRoot 'assets/icons/app.ico')",
    "/DAppImage=$appImage",
    "/DInstallerBackground=$wizardImage",
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
