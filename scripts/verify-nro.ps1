param([Parameter(Mandatory=$true)][string]$Path)
$ErrorActionPreference = 'Stop'
$full = [IO.Path]::GetFullPath($Path)
if (-not [IO.File]::Exists($full)) { throw "NRO missing: $full" }
# Bound allocations before reading an untrusted candidate.
if ((Get-Item -LiteralPath $full).Length -gt 64MB) { throw "NRO exceeds this project's 64 MiB candidate limit" }
$data = [IO.File]::ReadAllBytes($full)
if ($data.Length -lt 128) { throw "NRO too small" }
$ascii = [Text.Encoding]::ASCII
function Read-U32([long]$Offset) {
  if ($Offset -lt 0 -or $Offset + 4 -gt $data.Length) { throw "Truncated NRO field at $Offset" }
  return [long][BitConverter]::ToUInt32($data, [int]$Offset)
}
function Read-Magic([long]$Offset) {
  if ($Offset -lt 0 -or $Offset + 4 -gt $data.Length) { throw "Truncated NRO record at $Offset" }
  return $ascii.GetString($data, [int]$Offset, 4)
}
if ((Read-Magic 0x10) -ne 'NRO0') { throw "NRO0 header missing at 0x10" }
if ((Read-U32 0x14) -ne 0) { throw "Unsupported NRO header version" }
$imageSize = Read-U32 0x18
if ($imageSize -lt 128 -or $imageSize -gt $data.Length) { throw "NRO image size exceeds file or is too small" }
$previousEnd = 0L
$textSize = 0L
for ($segment = 0; $segment -lt 3; $segment++) {
  $offset = Read-U32 (0x20 + 8 * $segment)
  $size = Read-U32 (0x24 + 8 * $segment)
  if (($offset % 4096) -ne 0 -or ($size % 4096) -ne 0) { throw "Unaligned NRO segment $segment" }
  if ($offset -lt $previousEnd -or $offset + $size -gt $imageSize) { throw "Overlapping or out-of-bounds NRO segment $segment" }
  if ($segment -eq 0) {
    if ($offset -ne 0 -or $size -lt 128) { throw "Invalid NRO text segment" }
    $textSize = $size
  }
  $previousEnd = $offset + $size
}
if ($previousEnd -ne $imageSize) { throw "NRO segments do not cover the declared image" }
$module = Read-U32 4
$startupEnd = [Math]::Min(4096L, $textSize)
if ($module -lt 128 -or ($module % 4) -ne 0 -or $module + 28 -gt $startupEnd) { throw "Invalid MOD0 offset" }
if ((Read-Magic $module) -ne 'MOD0') { throw "MOD0 missing at declared offset" }
# Walk only the startup extension chain, never arbitrary strings in payloads.
$record = $module + 28
foreach ($extension in @('LNY0', 'LNY1', 'LNY2')) {
  if ($record + 12 -le $startupEnd -and (Read-Magic $record) -eq $extension) { $record += 12 }
}
if ($record + 12 -gt $startupEnd -or (Read-Magic $record) -ne 'OUN0') { throw "OUN0 startup record missing" }
if ((Read-U32 ($record + 4)) -ne 2) { throw "Platform is not Switch 2" }
if ((Read-U32 ($record + 8)) -ne 1) { throw "Unsupported OUN0 record version" }
$hasher = [Security.Cryptography.SHA256]::Create()
try { $sha = [BitConverter]::ToString($hasher.ComputeHash($data)).Replace('-', '') }
finally { $hasher.Dispose() }
Write-Host "[OK] NRO segment bounds and Switch 2 startup record verified"
Write-Host "[OK] Bytes: $($data.Length)"
Write-Host "[OK] SHA256: $sha"
Write-Host "[NOTE] Format checks do not establish runtime or hardware compatibility."
