param(
 [Parameter(Mandatory=$true)][string]$Nro,
 [Parameter(Mandatory=$true)][string]$Icon
)
$ErrorActionPreference='Stop'
if(-not [IO.File]::Exists($Icon)){
 throw "Missing Homebrew Menu icon: $Icon. Save the provided BackupLoaderMKW JPEG as assets\\BackupLoaderMKW.jpg next to build.bat, then build again."
}
$jpg=[IO.File]::ReadAllBytes($Icon)
if($jpg.Length -lt 16 -or $jpg.Length -gt 131072 -or
   $jpg[0] -ne 0xff -or $jpg[1] -ne 0xd8 -or
   $jpg[$jpg.Length-2] -ne 0xff -or $jpg[$jpg.Length-1] -ne 0xd9){
 throw 'Homebrew icon must be a valid JPEG no larger than 128 KiB.'
}
$bytes=[IO.File]::ReadAllBytes($Nro)
if($bytes.Length -lt 0x80 -or [Text.Encoding]::ASCII.GetString($bytes,0x10,4) -ne 'NRO0'){throw 'Not an NRO'}
$size=[BitConverter]::ToUInt32($bytes,0x18)
if($size -ne $bytes.Length){throw 'NRO already has trailing assets; refusing to overwrite existing metadata'}
# NRO homebrew ASET section: header + icon; other asset entries empty.
$header=New-Object byte[] 0x38
[Array]::Copy([Text.Encoding]::ASCII.GetBytes('ASET'),0,$header,0,4)
[Array]::Copy([BitConverter]::GetBytes([uint64]0x38),0,$header,8,8)
[Array]::Copy([BitConverter]::GetBytes([uint64]$jpg.Length),0,$header,16,8)
$stream=[IO.File]::Open($Nro,[IO.FileMode]::Append,[IO.FileAccess]::Write)
try{
 $stream.Write($header,0,$header.Length)
 $stream.Write($jpg,0,$jpg.Length)
 $stream.Flush()
}finally{$stream.Dispose()}
$check=[IO.File]::ReadAllBytes($Nro)
if([Text.Encoding]::ASCII.GetString($check,[int]$size,4) -ne 'ASET' -or
   [BitConverter]::ToUInt64($check,[int]$size+16) -ne $jpg.Length){
 throw 'Icon asset validation failed'
}
Write-Output "[OK] Embedded Homebrew Menu icon ($($jpg.Length) bytes)"
