param(
    [string]$Generator = 'Ninja',
    [string]$Compiler = 'g++',
    [string]$ToolPath = ''
)
$ErrorActionPreference = 'Stop'
$testsDir = $PSScriptRoot
$buildDir = Join-Path $testsDir 'build-host'

# Inside the monorepo, use its pinned CMake/Ninja; -ToolPath prepends a compiler directory.
$vendorTools = [IO.Path]::GetFullPath((Join-Path $testsDir '../../../../vendor/totk-dkp/tools'))
if (Test-Path -LiteralPath $vendorTools) {
    $env:PATH = "$vendorTools;$(Join-Path $vendorTools 'cmake-3.30.5-windows-x86_64/bin');$env:PATH"
}
if ($ToolPath) { $env:PATH = "$ToolPath;$env:PATH" }

foreach ($tool in 'cmake', 'ctest', $Compiler) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool not found on PATH" }
}

cmake -S $testsDir -B $buildDir -G $Generator "-DCMAKE_BUILD_TYPE=Debug" "-DCMAKE_CXX_COMPILER=$Compiler"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
ctest --test-dir $buildDir --output-on-failure --no-tests=error
exit $LASTEXITCODE
