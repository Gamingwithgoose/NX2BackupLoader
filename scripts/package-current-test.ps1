param([Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference='Stop'
function File-Sha256([string]$Path){
 $stream=[IO.File]::OpenRead($Path);$sha=[Security.Cryptography.SHA256]::Create()
 try{return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}
 finally{$stream.Dispose();$sha.Dispose()}
}
$repo=[IO.Path]::GetFullPath($Root)
$release=Join-Path $repo 'release'
$app=Join-Path $release 'switch\BackupLoaderMKW'
$runtime=Join-Path $app 'runtime'
New-Item -ItemType Directory -Force -Path $runtime | Out-Null
$pairs=@(
 @{Source=(Join-Path $repo 'probes\library-ui\BackupLoaderMKW-UI.nro');Target=(Join-Path $app 'BackupLoaderMKW.nro');Icon=$true},
 @{Source=(Join-Path $repo 'probes\game-launch\BackupLoaderMKW.nro');Target=(Join-Path $runtime 'BackupLoaderMKW-core.nro');Icon=$false}
)
try {
 foreach($pair in $pairs){
  & (Join-Path $repo 'scripts\verify-nro.ps1') -Path $pair.Source
  if(-not $?){throw 'Executable verification failed'}
  $stage=$pair.Target+'.tmp'
  Copy-Item -LiteralPath $pair.Source -Destination $stage
  if((File-Sha256 $stage) -ne (File-Sha256 $pair.Source)){throw 'Staged executable mismatch'}
  if($pair.Icon){
   & (Join-Path $repo 'scripts\embed-icon.ps1') -Nro $stage -Icon (Join-Path $repo 'assets\BackupLoaderMKW.jpg')
   if(-not $?){throw 'Icon embedding failed'}
  }
  & (Join-Path $repo 'scripts\verify-nro.ps1') -Path $stage
  if(-not $?){throw 'Staged executable verification failed'}
 }
 # Both candidates are verified before either is published; failure removes the pair.
 foreach($pair in $pairs){Move-Item -LiteralPath ($pair.Target+'.tmp') -Destination $pair.Target -Force}
} catch {
 foreach($pair in $pairs){
  foreach($path in @($pair.Target,($pair.Target+'.tmp'))){if(Test-Path -LiteralPath $path){Remove-Item -LiteralPath $path -Force}}
 }
 throw
}
Copy-Item -LiteralPath (Join-Path $repo 'README.md') -Destination (Join-Path $release 'README.md')
# Package the GUI and runtime together. No game content or user settings are staged.
# Remove stale package names from older local builds (not user SD data).
foreach($relative in @('switch\nx2-folder-exec','nx2backuploader.nro','nx2-sdprobe.nro')){
 $old=[IO.Path]::GetFullPath((Join-Path $release $relative))
 if(-not $old.StartsWith($release+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Cleanup path escapes release'}
 if(Test-Path -LiteralPath $old){Remove-Item -LiteralPath $old -Recurse -Force}
}
Write-Output '[OK] GUI and runtime staged in release/switch; copy the entire folder to your SD card.'
