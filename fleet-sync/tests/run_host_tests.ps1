$ErrorActionPreference = 'Stop'
$testsDir = $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $testsDir '..\..\..\..')).Path
$cmake = Join-Path $repoRoot 'vendor\totk-dkp\tools\cmake-3.30.5-windows-x86_64\bin\cmake.exe'
$ctest = Join-Path $repoRoot 'vendor\totk-dkp\tools\cmake-3.30.5-windows-x86_64\bin\ctest.exe'
$ninjaDir = Join-Path $repoRoot 'vendor\totk-dkp\tools'
if (-not (Test-Path $cmake)) {
    $cmake = (Get-Command cmake -ErrorAction Stop).Source
    $ctest = (Get-Command ctest -ErrorAction Stop).Source
    $ninjaDir = Split-Path (Get-Command ninja -ErrorAction Stop).Source
}
$buildDir = Join-Path $testsDir 'build-host'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found - install VS Build Tools" }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "no MSVC toolset found by vswhere" }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'

$cmd = "set `"PATH=$ninjaDir;%PATH%`" && `"$vcvars`" >nul" +
       " && `"$cmake`" -S `"$testsDir`" -B `"$buildDir`" -G Ninja -DCMAKE_BUILD_TYPE=Debug" +
       " && `"$cmake`" --build `"$buildDir`"" +
       " && `"$ctest`" --test-dir `"$buildDir`" --output-on-failure --no-tests=error"
& cmd.exe /d /c $cmd
exit $LASTEXITCODE
