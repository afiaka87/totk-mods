$ErrorActionPreference = 'Stop'

$testsDir = $PSScriptRoot
$buildDir = Join-Path $testsDir 'build-host'

# Inside the monorepo, use its pinned CMake, Ninja and compiler; elsewhere take them from PATH.
$pinned = [IO.Path]::GetFullPath((Join-Path $testsDir '../../../../scripts/host_test_tools.ps1'))
if (Test-Path -LiteralPath $pinned) {
    . $pinned
} else {
    $cmake = 'cmake'
    $ctest = 'ctest'
    $compiler = if ($env:CXX) { $env:CXX } else { 'g++' }
    foreach ($tool in $cmake, $ctest, 'ninja', $compiler) {
        if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool not found on PATH" }
    }
    $compiler = (Get-Command $compiler).Source
}

$env:PATH = ((Split-Path -Parent $compiler), $env:PATH) -join [IO.Path]::PathSeparator
& $cmake -S $testsDir -B $buildDir -G Ninja `
    "-DCMAKE_BUILD_TYPE=Debug" "-DCMAKE_CXX_COMPILER=$compiler"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $buildDir
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $ctest --test-dir $buildDir --output-on-failure --no-tests=error
exit $LASTEXITCODE
