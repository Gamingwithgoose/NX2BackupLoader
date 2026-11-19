param([Parameter(Mandatory=$true)][string]$Path)
$ErrorActionPreference = 'Stop'
$full = [IO.Path]::GetFullPath($Path)
if (-not [IO.File]::Exists($full)) { throw "NRO missing: $full" }
$data = [IO.File]::ReadAllBytes($full)
if ($data.Length -lt 128) { throw "NRO too small" }
$ascii = [Text.Encoding]::ASCII
if ($ascii.GetString($data, 0x10, 4) -ne "NRO0") { throw "NRO0 header missing at 0x10" }
# Switch 2 libounce CRT emits an OUN0 extension. This is only a smoke test.
$found = $false
for ($i=0; $i -le $data.Length-4; $i++) {
  if ($data[$i] -eq 0x4f -and $data[$i+1] -eq 0x55 -and $data[$i+2] -eq 0x4e -and $data[$i+3] -eq 0x30) { $found = $true; break }
}
if (-not $found) { throw "Missing Switch 2 OUN0 marker; reject possible Switch 1 NRO" }
$sha = (Get-FileHash -Algorithm SHA256 -LiteralPath $full).Hash
Write-Host "[OK] NRO0 + Switch 2 OUN0 marker present"
Write-Host "[OK] Bytes: $($data.Length)"
Write-Host "[OK] SHA256: $sha"
