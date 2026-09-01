param(
    # Your own dump of the TotK 1.2.1 main executable, relocated to base 0.
    [string]$Binary = ''
)
$ErrorActionPreference = 'Stop'
if (-not $Binary) {
    $Binary = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../work/main121.relocated.bin'))
    if (-not (Test-Path -LiteralPath $Binary)) { throw 'pass -Binary <path to your TotK 1.2.1 main dump>' }
}
if (-not (Test-Path -LiteralPath $Binary)) { throw "missing TotK 1.2.1 binary: $Binary" }

$expected = [ordered]@{
    0x0175CA00 = '52A83388'
    0x0175CA04 = 'BD42B70A'
    0x0175CC24 = '52A8339B'
    0x0175CC28 = '5283EEBC'
    0x0175C99C = 'D10483FF'
    0x01C64CE4 = '1E269000'
    0x01C64CE8 = 'D65F03C0'
    0x00E50444 = 'BD42A903'
    0x00E50448 = 'BD42BEE2'
    0x00E501DC = 'A9BB7BFD'
    0x00829FD8 = '79800408'
    0x01DAF314 = 'A9BD7BFD'
}

$stream = [System.IO.File]::OpenRead($Binary)
$failed = $false
try {
    foreach ($entry in $expected.GetEnumerator()) {
        $stream.Position = [int64]$entry.Key
        $bytes = New-Object byte[] 4
        [void]$stream.Read($bytes, 0, 4)
        $actual = [BitConverter]::ToUInt32($bytes, 0)
        $wanted = [Convert]::ToUInt32([string]$entry.Value, 16)
        if ($actual -ne $wanted) {
            Write-Host ('FAIL main+0x{0:X8}: actual 0x{1:X8}, expected 0x{2:X8}' -f $entry.Key,$actual,$wanted) -ForegroundColor Red
            $failed = $true
        }
    }
} finally {
    $stream.Dispose()
}

if ($failed) { exit 1 }
Write-Host "PASS: $($expected.Count) Zonai Ascend words match TotK 1.2.1 build 9B4E43650501A4D4." -ForegroundColor Green
