param(
    [string]$Elf = (Join-Path $PSScriptRoot '../build/subsdk9.elf'),
    # devkitA64 nm: the monorepo's vendored copy, else $env:DEVKITPRO, else PATH.
    [string]$Nm = ''
)
$ErrorActionPreference = 'Stop'
if (-not $Nm) {
    $vendored = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../vendor/totk-dkp/root/opt/devkitpro/devkitA64/bin/aarch64-none-elf-nm.exe'))
    if (Test-Path -LiteralPath $vendored) { $Nm = $vendored }
    elseif ($env:DEVKITPRO) { $Nm = "$env:DEVKITPRO/devkitA64/bin/aarch64-none-elf-nm" }
    else { $Nm = 'aarch64-none-elf-nm' }
}
if (-not (Test-Path -LiteralPath $Elf)) { throw "missing linked ELF: $Elf" }
if (-not (Get-Command $Nm -ErrorAction SilentlyContinue)) { throw "devkitA64 nm not found: $Nm" }

$undefined = & $Nm -u $Elf
if ($LASTEXITCODE -ne 0) { throw "nm failed with exit code $LASTEXITCODE" }

$forbidden = @(
    '_ZN2nn2os17ConvertToTimeSpanEm'
)

$found = @($undefined | Where-Object {
    $line = $_
    $forbidden | Where-Object { $line -match [regex]::Escape($_) }
})

if ($found.Count -gt 0) {
    $found | ForEach-Object { Write-Host "FAIL forbidden unresolved runtime import: $_" -ForegroundColor Red }
    exit 1
}

Write-Host 'PASS: no forbidden unresolved Zonai Ascend runtime imports.' -ForegroundColor Green
