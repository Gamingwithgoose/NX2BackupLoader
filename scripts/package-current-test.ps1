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
Move-Item -LiteralPath $stage -Destination $target -Force
Copy-Item -LiteralPath (Join-Path $repo 'README.md') -Destination (Join-Path $release 'README.md')
# Folder indices are now generated from the selected SD backup. Retire metadata only.
$oldMap=[IO.Path]::GetFullPath((Join-Path $release 'games\nx2-yoshi\romfs.map'))
if(-not $oldMap.StartsWith($release+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Cleanup path escapes release'}
if(Test-Path -LiteralPath $oldMap){Remove-Item -LiteralPath $oldMap}
foreach($relative in @('games\nx2-yoshi','games')){
 $oldDir=[IO.Path]::GetFullPath((Join-Path $release $relative))
 if(-not $oldDir.StartsWith($release+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Cleanup path escapes release'}
 if((Test-Path -LiteralPath $oldDir) -and @(Get-ChildItem -LiteralPath $oldDir -Force).Count -eq 0){Remove-Item -LiteralPath $oldDir}
}
# Retire only named obsolete build artifacts, keeping all hardware reports.
foreach($relative in @('nx2backuploader.nro','nx2-sdprobe.nro','BackupLoaderMKW.nro','games\nx2-exec-fixture\module.nxm','games\nx2-exec-fixture\provider.nxm','games\nx2-exec-fixture\client.nxm')){
 $target=[IO.Path]::GetFullPath((Join-Path $release $relative))
 if(-not $target.StartsWith($release+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Cleanup path escapes release'}
 if(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target -Force}
}
$obsolete=[IO.Path]::GetFullPath((Join-Path $release 'games\nx2-exec-fixture'))
if(-not $obsolete.StartsWith($release+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Cleanup path escapes release'}
if((Test-Path -LiteralPath $obsolete) -and @(Get-ChildItem -LiteralPath $obsolete -Force).Count -eq 0){Remove-Item -LiteralPath $obsolete}
Write-Output '[OK] Folder Launcher 0.13 staged in release/switch; game indices are automatic.'
