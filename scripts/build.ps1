param([switch]$Test)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$clionBin = Join-Path $env:LOCALAPPDATA 'Programs\CLion\bin'
$cmake = Join-Path $clionBin 'cmake\win\x64\bin\cmake.exe'
$ninja = Join-Path $clionBin 'ninja\win\x64\ninja.exe'
$compiler = Join-Path $clionBin 'mingw\bin\g++.exe'
$qtRoot = Join-Path $projectRoot '.deps\Qt'
foreach ($required in @($cmake,$ninja,$compiler,(Join-Path $qtRoot 'lib\cmake\Qt6\Qt6Config.cmake'))) {
    if (!(Test-Path -LiteralPath $required)) { throw "Missing build dependency: $required. See README.md for manual CMake setup." }
}
$env:PATH = (Join-Path $qtRoot 'bin') + ';' + (Split-Path $compiler) + ';' + $env:PATH
$buildDir = Join-Path $projectRoot 'build'
& $cmake -S $projectRoot -B $buildDir -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_CXX_COMPILER=$compiler" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
if ($LASTEXITCODE) { throw 'CMake configuration failed.' }
& $cmake --build $buildDir -j 6
if ($LASTEXITCODE) { throw 'Build failed.' }
if ($Test) {
    & (Join-Path (Split-Path $cmake) 'ctest.exe') --test-dir $buildDir --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed.' }
}
Write-Output "Ready: $buildDir\RockAutoMusicPlay.exe"
