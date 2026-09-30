param(
    [string]$Generator = 'Ninja',
    [string]$Config = 'Debug'
)
$ErrorActionPreference = 'Stop'
$testsDir = $PSScriptRoot
$buildDir = Join-Path $testsDir 'build-host'

$vendorTools = [IO.Path]::GetFullPath((Join-Path $testsDir '../../../../vendor/totk-dkp/tools'))
if (Test-Path -LiteralPath $vendorTools) {
    $env:PATH = "$vendorTools;$(Join-Path $vendorTools 'cmake-3.30.5-windows-x86_64/bin');$env:PATH"
}
foreach ($tool in 'cmake', 'ctest') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool not found on PATH" }
}

# Outside a Developer shell on Windows, enter vcvars64 once and re-run this script inside it.
if ($env:OS -eq 'Windows_NT' -and -not $env:VCToolsInstallDir -and -not $env:TOTK_HOST_TESTS_REENTERED) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $vcvars = if ($vsPath) { Join-Path $vsPath 'VC/Auxiliary/Build/vcvars64.bat' } else { '' }
        if ($vcvars -and (Test-Path -LiteralPath $vcvars)) {
            $env:TOTK_HOST_TESTS_REENTERED = '1'
            $self = $MyInvocation.MyCommand.Path
            $cmd = "set `"PATH=$(Split-Path $vswhere);%PATH%`" && call `"$vcvars`" >nul 2>nul" +
                   " && powershell -NoProfile -ExecutionPolicy Bypass -File `"$self`" -Generator `"$Generator`"" +
                   " -Config `"$Config`""
            & cmd.exe /d /c $cmd
            exit $LASTEXITCODE
        }
    }
}

cmake -S $testsDir -B $buildDir -G $Generator "-DCMAKE_BUILD_TYPE=$Config"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $buildDir --config $Config
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ctest --test-dir $buildDir --build-config $Config --output-on-failure --no-tests=error
$ctestExit = $LASTEXITCODE

# One unfiltered run reports doctest's own case count, independent of CTest discovery.
Write-Host "`n--- unfiltered doctest run (ground truth) ---"
$exe = Get-ChildItem -Path $buildDir -Recurse -Filter 'zonai_hookshot_pure_tests*' -File |
    Where-Object { $_.Extension -in '', '.exe' } | Select-Object -First 1
& $exe.FullName
$directExit = $LASTEXITCODE
if ($ctestExit -ne 0) { exit $ctestExit }
exit $directExit
