param([Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference='Stop'
function File-Sha256([string]$Path){
 $stream=[IO.File]::OpenRead($Path);$sha=[Security.Cryptography.SHA256]::Create()
 try{return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}
 finally{$stream.Dispose();$sha.Dispose()}
}
$repo=[IO.Path]::GetFullPath($Root)
$source=Join-Path $repo 'probes\game-launch'
$release=Join-Path $repo 'release'
$binary=Join-Path $source 'BackupLoaderMKW.nro'
& (Join-Path $repo 'scripts\verify-nro.ps1') -Path $binary
if(-not $?){throw 'Executable verification failed'}
$app=Join-Path $release 'switch\BackupLoaderMKW'
New-Item -ItemType Directory -Force -Path $app | Out-Null
$target=Join-Path $app 'BackupLoaderMKW.nro'
$stage=$target+'.tmp';Copy-Item -LiteralPath $binary -Destination $stage
if((File-Sha256 $stage) -ne (File-Sha256 $binary)){throw 'Staged executable mismatch'}
& (Join-Path $repo 'scripts\embed-icon.ps1') -Nro $stage -Icon (Join-Path $repo 'assets\BackupLoaderMKW.jpg')
if(-not $?){throw 'Icon embedding failed'}
Move-Item -LiteralPath $stage -Destination $target -Force
Copy-Item -LiteralPath (Join-Path $repo 'README.md') -Destination (Join-Path $release 'README.md')
# Only the launcher NRO and its README are included in the release folder.
# Remove stale package names from older local builds (not user SD data).
foreach($relative in @('switch\nx2-folder-exec','nx2backuploader.nro','nx2-sdprobe.nro')){
 $old=[IO.Path]::GetFullPath((Join-Path $release $relative))
 if(-not $old.StartsWith($release+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Cleanup path escapes release'}
 if(Test-Path -LiteralPath $old){Remove-Item -LiteralPath $old -Recurse -Force}
}
Write-Output '[OK] BackupLoaderMKW staged in release/switch; game indices are generated on the console.'
