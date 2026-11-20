param([Parameter(Mandatory=$true)][string]$Root)
$ErrorActionPreference='Stop'
function File-Sha256([string]$Path){
 $stream=[IO.File]::OpenRead($Path);$sha=[Security.Cryptography.SHA256]::Create()
 try{return [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant()}
 finally{$stream.Dispose();$sha.Dispose()}
}
$repo=[IO.Path]::GetFullPath($Root)
$source=Join-Path $repo 'probes\folder-suite'
$release=Join-Path $repo 'release'
$binary=Join-Path $source 'nx2-folder-exec.nro'
& (Join-Path $repo 'scripts\verify-nro.ps1') -Path $binary
if(-not $?){throw 'Executable verification failed'}
$references=Get-Content -Raw -LiteralPath (Join-Path $source 'fixture\reference.json') | ConvertFrom-Json
foreach($row in $references){
 if($row.name -notin @('provider','client')){throw 'Unexpected fixture name'}
 $path=Join-Path $source ('fixture\'+$row.name+'.nxm')
 $hash=(File-Sha256 $path)
 if($hash -ne $row.sha256){throw 'Fixture hash mismatch'}
}
$app=Join-Path $release 'switch\nx2-folder-exec'
$games=Join-Path $release 'games\nx2-exec-fixture'
New-Item -ItemType Directory -Force -Path $app,$games | Out-Null
$files=@(@{source=$binary;target=(Join-Path $app 'nx2-folder-exec.nro')})
foreach($row in $references){$files+=@{source=(Join-Path $source ('fixture\'+$row.name+'.nxm'));target=(Join-Path $games ($row.name+'.nxm'))}}
foreach($file in $files){
 $stage=$file.target+'.tmp';Copy-Item -LiteralPath $file.source -Destination $stage
 if((File-Sha256 $stage) -ne (File-Sha256 $file.source)){throw 'Staged copy mismatch'}
}
# Publish the executable last, after both matching fixtures are in place.
foreach($file in ($files | Select-Object -Skip 1)){Move-Item -LiteralPath ($file.target+'.tmp') -Destination $file.target -Force}
Move-Item -LiteralPath ($files[0].target+'.tmp') -Destination $files[0].target -Force
Copy-Item -LiteralPath (Join-Path $source 'README.txt') -Destination (Join-Path $release 'README.txt')
# Only named outputs from earlier tests are retired; game directories are never removed.
foreach($relative in @('nx2backuploader.nro','nx2-sdprobe.nro','nx2-folder-exec.nro','games\nx2-exec-fixture\module.nxm')){
 $target=[IO.Path]::GetFullPath((Join-Path $release $relative))
 if(-not $target.StartsWith($release+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Cleanup path escapes release'}
 if(Test-Path -LiteralPath $target){Remove-Item -LiteralPath $target -Force}
}
Write-Output '[OK] Current test staged in release/switch and release/games.'
