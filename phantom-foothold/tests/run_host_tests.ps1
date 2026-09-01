param(
    [string]$Generator = 'Ninja',
    [string]$Config = 'Debug',
    # Optional C++ compiler path; when empty CMake picks one (MSVC on Windows via vcvars below).
    [string]$Compiler = ''
)
$ErrorActionPreference = 'Stop'
$testsDir = $PSScriptRoot
$buildDir = Join-Path $testsDir 'build-host'

# Inside the monorepo use its vendored CMake and Ninja; standalone, use whatever is on PATH.
$vendorTools = [IO.Path]::GetFullPath((Join-Path $testsDir '../../../../vendor/totk-dkp/tools'))
if (Test-Path -LiteralPath $vendorTools) {
    $env:PATH = "$vendorTools;$(Join-Path $vendorTools 'cmake-3.30.5-windows-x86_64/bin');$env:PATH"
}
foreach ($tool in 'cmake', 'ctest') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool not found on PATH" }
}

# On Windows outside a Developer shell (and with no explicit compiler), re-enter through vcvars64.
if ($env:OS -eq 'Windows_NT' -and -not $Compiler -and -not $env:VCToolsInstallDir -and -not $env:TOTK_HOST_TESTS_REENTERED) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $vcvars = if ($vsPath) { Join-Path $vsPath 'VC/Auxiliary/Build/vcvars64.bat' } else { '' }
        if ($vcvars -and (Test-Path -LiteralPath $vcvars)) {
            $env:TOTK_HOST_TESTS_REENTERED = '1'
            $self = $MyInvocation.MyCommand.Path
            $cmd = "set `"PATH=$(Split-Path $vswhere);%PATH%`" && call `"$vcvars`" >nul" +
                   " && powershell -NoProfile -ExecutionPolicy Bypass -File `"$self`" -Generator `"$Generator`" -Config `"$Config`""
            & cmd.exe /d /c $cmd
            exit $LASTEXITCODE
        }
    }
}

$configureArgs = @('-S', $testsDir, '-B', $buildDir, '-G', $Generator, "-DCMAKE_BUILD_TYPE=$Config")
if ($Compiler) { $configureArgs += "-DCMAKE_CXX_COMPILER=$Compiler" }
cmake @configureArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $buildDir --config $Config
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ctest --test-dir $buildDir --build-config $Config --output-on-failure --no-tests=error
exit $LASTEXITCODE
