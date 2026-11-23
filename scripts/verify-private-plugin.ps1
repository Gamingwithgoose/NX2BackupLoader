param([Parameter(Mandatory=$true)][string]$Path)
$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Private plugin output missing: $Path" }
$item = Get-Item -LiteralPath $Path
if ($item.Length -lt 256 -or $item.Length -gt 32MB) { throw "Unexpected NSO size: $($item.Length)" }
$stream = [System.IO.File]::OpenRead($item.FullName)
try {
  $magic = New-Object byte[] 4
  if ($stream.Read($magic, 0, 4) -ne 4) { throw "Could not read NSO header" }
  if ([System.Text.Encoding]::ASCII.GetString($magic) -ne "NSO0") {
    throw "Private plugin is not an NSO0 executable"
  }
} finally { $stream.Dispose() }
Write-Host "[VERIFIED] Developer-only Switch 2 plugin NSO: $($item.FullName) ($($item.Length) bytes)"
