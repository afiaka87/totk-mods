$ErrorActionPreference = 'Stop'
$testsDir = $PSScriptRoot
$buildDir = Join-Path $testsDir 'build-host'

# Inside the monorepo, use its pinned tools; elsewhere take CMake, CTest and Ninja from PATH and MSVC from vswhere.
$pinned = [IO.Path]::GetFullPath((Join-Path $testsDir '../../../../scripts/host_test_tools.ps1'))
if (Test-Path -LiteralPath $pinned) {
    . $pinned
} else {
    $cmake = (Get-Command cmake -ErrorAction Stop).Source
    $ctest = (Get-Command ctest -ErrorAction Stop).Source
    $ninjaDir = Split-Path (Get-Command ninja -ErrorAction Stop).Source
    function Get-HostBuildScope {
        if ($env:OS -ne 'Windows_NT') { return 'true' }
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found - install VS Build Tools' }
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if (-not $vsPath) { throw 'no MSVC toolset found by vswhere' }
        $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
        return "set `"PATH=$ninjaDir;%PATH%`" && `"$vcvars`" >nul"
    }
    function Invoke-HostShell([string]$Command) {
        if ($env:OS -eq 'Windows_NT') { & cmd.exe /d /c $Command } else { & sh -c $Command }
    }
}

$cmd = (Get-HostBuildScope) +
       " && `"$cmake`" -S `"$testsDir`" -B `"$buildDir`" -G Ninja -DCMAKE_BUILD_TYPE=Debug" +
       " && `"$cmake`" --build `"$buildDir`"" +
       " && `"$ctest`" --test-dir `"$buildDir`" --output-on-failure --no-tests=error"
Invoke-HostShell $cmd
exit $LASTEXITCODE
