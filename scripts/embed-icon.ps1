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
# Standard libnx/Atmosphere homebrew ASET: icon + 0x4000-byte NACP.
# The original ounce-devkit build emits no NACP (NO_NACP=1), so append
# ordinary Homebrew Menu metadata without changing the Switch 2 ELF/startup.
# NACP language records each contain 0x200-byte name and 0x100-byte author.
$nacp=New-Object byte[] 0x4000
$nameBytes=[Text.Encoding]::UTF8.GetBytes('BackupLoaderMKW')
$authorBytes=[Text.Encoding]::UTF8.GetBytes('Goose')
if($nameBytes.Length -ge 0x200 -or $authorBytes.Length -ge 0x100){
 throw 'NACP title/author exceeds fixed language record limits'
}
for($language=0;$language -lt 16;$language++){
 $languageOffset=$language*0x300
 [Array]::Copy($nameBytes,0,$nacp,$languageOffset,$nameBytes.Length)
 [Array]::Copy($authorBytes,0,$nacp,$languageOffset+0x200,$authorBytes.Length)
}
$iconOffset=[uint64]0x38
$nacpOffset=$iconOffset+[uint64]$jpg.Length
$header=New-Object byte[] 0x38
[Array]::Copy([Text.Encoding]::ASCII.GetBytes('ASET'),0,$header,0,4)
[Array]::Copy([BitConverter]::GetBytes($iconOffset),0,$header,8,8)
[Array]::Copy([BitConverter]::GetBytes([uint64]$jpg.Length),0,$header,16,8)
[Array]::Copy([BitConverter]::GetBytes($nacpOffset),0,$header,0x28,8)
[Array]::Copy([BitConverter]::GetBytes([uint64]$nacp.Length),0,$header,0x30,8)
$stream=[IO.File]::Open($Nro,[IO.FileMode]::Append,[IO.FileAccess]::Write)
try{
 $stream.Write($header,0,$header.Length)
 $stream.Write($jpg,0,$jpg.Length)
 $stream.Write($nacp,0,$nacp.Length)
 $stream.Flush()
}finally{$stream.Dispose()}
$check=[IO.File]::ReadAllBytes($Nro)
$asetStart=[int]$size
if([Text.Encoding]::ASCII.GetString($check,$asetStart,4) -ne 'ASET' -or
   [BitConverter]::ToUInt64($check,$asetStart+16) -ne $jpg.Length -or
   [BitConverter]::ToUInt64($check,$asetStart+0x28) -ne $nacpOffset -or
   [BitConverter]::ToUInt64($check,$asetStart+0x30) -ne $nacp.Length){
 throw 'NRO icon/NACP asset header validation failed'
}
$actualAuthor=[Text.Encoding]::UTF8.GetString($check,
  $asetStart+[int]$nacpOffset+0x200,$authorBytes.Length)
if($actualAuthor -cne 'Goose'){throw 'Packaged NACP Author does not match Goose'}
Write-Output "[OK] Embedded Homebrew Menu icon ($($jpg.Length) bytes)"
Write-Output '[OK] NACP Homebrew Menu author: Goose'
