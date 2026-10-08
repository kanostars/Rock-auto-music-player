param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') }
$cmakeText = Get-Content -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt') -Raw
if ($cmakeText -notmatch 'project\(RockAutoMusicPlay VERSION (\d+\.\d+\.\d+)') { throw 'Cannot determine application version.' }
$version = $Matches[1]
$name = "RockAutoMusicPlay-$version-windows-x64"
$releaseRoot = Join-Path $projectRoot 'artifacts/releases'
$packageDir = Join-Path $releaseRoot $name
$zipPath = Join-Path $releaseRoot "$name.zip"
if ((Test-Path -LiteralPath $packageDir) -or (Test-Path -LiteralPath $zipPath)) { throw "Release output already exists: $packageDir. Preserve or move it before packaging again." }
$buildDir = Join-Path $projectRoot 'build'
$qtRoot = Join-Path $projectRoot '.deps/Qt'
$compilerBin = Join-Path $env:LOCALAPPDATA 'Programs/CLion/bin/mingw/bin'
$env:PATH = (Join-Path $qtRoot 'bin') + ';' + $compilerBin + ';' + $buildDir + ';' + $env:PATH
New-Item -ItemType Directory -Path $packageDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $buildDir 'RockAutoMusicPlay.exe') -Destination $packageDir
foreach ($dll in @('interception.dll','libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll')) {
    Copy-Item -LiteralPath (Join-Path $buildDir $dll) -Destination $packageDir
}
& (Join-Path $qtRoot 'bin/windeployqt.exe') --release --no-translations --no-compiler-runtime --no-opengl-sw --skip-plugin-types generic,networkinformation,tls --exclude-plugins qdirect2d,qminimal,qoffscreen --dir $packageDir (Join-Path $packageDir 'RockAutoMusicPlay.exe')
if ($LASTEXITCODE) { throw 'Qt runtime deployment failed.' }
Copy-Item -LiteralPath (Join-Path $projectRoot 'README.md'),(Join-Path $projectRoot 'LICENSE') -Destination $packageDir
Copy-Item -LiteralPath (Join-Path $projectRoot 'samples') -Destination $packageDir -Recurse
$licensesDir = Join-Path $packageDir 'licenses'
New-Item -ItemType Directory -Path $licensesDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/SOURCES.md') -Destination (Join-Path $licensesDir 'SOURCES.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/midifile/LICENSE.txt') -Destination (Join-Path $licensesDir 'Midifile-BSD-2-Clause.txt')
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/miniaudio/LICENSE') -Destination (Join-Path $licensesDir 'miniaudio.txt')
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/interception/LICENSE.txt') -Destination (Join-Path $licensesDir 'Interception.txt')
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets/handpan/README.md') -Destination (Join-Path $licensesDir 'Handpan-samples.md')
Copy-Item -LiteralPath (Join-Path $projectRoot 'assets/handpan/manifest.json') -Destination (Join-Path $licensesDir 'Handpan-manifest.json')
Copy-Item -LiteralPath (Join-Path $projectRoot 'third_party/runtime-licenses/README.md') -Destination (Join-Path $licensesDir 'Runtime-sources.md')
foreach ($license in @('LGPL-3.0-only.txt','GPL-3.0-only.txt','GCC-exception-3.1.txt')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "third_party/runtime-licenses/$license") -Destination $licensesDir
}
@"
RockAutoMusicPlay $version · Windows x64 便携版

1. 完整解压 ZIP 到一个文件夹。
2. 双击 RockAutoMusicPlay.exe 启动。
3. 导入自己的 MIDI，或使用 samples/studio-demo.mid 体验。

请保留 EXE 同目录的所有 DLL、platforms 与其他插件目录。
手碟试听和跟练可直接使用；游戏自动演奏需要已安装并正常工作的 Interception 驱动。
驱动说明：https://github.com/oblitum/Interception
完整功能和按键说明见 README.md，第三方来源及许可见 licenses 文件夹。
"@ | Set-Content -LiteralPath (Join-Path $packageDir '使用说明.txt') -Encoding utf8
$packageExe = Join-Path $packageDir 'RockAutoMusicPlay.exe'
if ((Get-Item -LiteralPath $packageExe).VersionInfo.ProductVersion -ne $version) { throw 'Packaged executable version does not match source.' }
$hashLines = Get-ChildItem -LiteralPath $packageDir -Recurse -File | Sort-Object FullName | ForEach-Object {
    $relative = [IO.Path]::GetRelativePath($packageDir,$_.FullName).Replace('\','/')
    "$( (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() )  $relative"
}
$hashLines | Set-Content -LiteralPath (Join-Path $packageDir 'SHA256SUMS.txt') -Encoding utf8
Compress-Archive -LiteralPath $packageDir -DestinationPath $zipPath -CompressionLevel Optimal
$zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
"$zipHash  $name.zip" | Set-Content -LiteralPath (Join-Path $releaseRoot 'SHA256SUMS.txt') -Encoding ascii
Write-Output "Release package: $zipPath"
Write-Output "SHA256: $zipHash"
