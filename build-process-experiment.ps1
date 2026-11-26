param([string]$DevkitPro='C:/devkitPro')
$ErrorActionPreference='Stop'
$repo=$PSScriptRoot
$source=Join-Path $repo 'probes/game-launch/source'
$experiment=Join-Path $repo 'probes/process-launch/source'
$out=Join-Path $repo '.work/process-experiment1/native'
$release=Join-Path $repo 'release/switch/BackupLoaderProcessExperiment'
$compiler=Join-Path $DevkitPro 'devkitA64/bin/aarch64-none-elf-gcc.exe'
New-Item -ItemType Directory -Force -Path $out,$release | Out-Null
$flags=@('-g','-Wall','-Wextra','-Werror','-O2','-ffunction-sections','-mcpu=cortex-a78c+crypto','-mtp=soft','-fPIE','-D__SWITCH__','-D__SWITCH2__','-ffreestanding','-fno-builtin',"-I$DevkitPro/libounce/include","-I$DevkitPro/portlibs/switch/include","-I$DevkitPro/portlibs/switch2/include")
# Separate main and output; the normal launcher and its packaged NRO are never overwritten.
$inputs=@(Get-ChildItem -LiteralPath $source -File | Where-Object { $_.Extension -in @('.c','.S') -and $_.Name -notin @('main.c','fault.c') }) + @(Get-ChildItem -LiteralPath $experiment -Filter *.c -File)
$objects=@()
foreach($inputFile in $inputs){
 $object=Join-Path $out ($inputFile.BaseName+'.o')
 & $compiler @flags -c $inputFile.FullName -o $object
 if($LASTEXITCODE -ne 0){throw "Compilation failed: $($inputFile.Name)"}
 $objects+=$object
}
$env:DEVKITPRO=$DevkitPro
$elf=Join-Path $out 'BackupLoaderProcessExperiment.elf'
$nro=Join-Path $release 'BackupLoaderProcessExperiment.nro'
$candidate=Join-Path $out 'BackupLoaderProcessExperiment.nro'
& $compiler "-specs=$DevkitPro/libounce/switch.specs" -g '-mcpu=cortex-a78c+crypto' -mtp=soft -fPIE "-Wl,-Map,$out/BackupLoaderProcessExperiment.map" @objects "-L$DevkitPro/portlibs/switch2/lib" "-L$DevkitPro/portlibs/switch/lib" "-L$DevkitPro/libounce/lib" -lnx -o $elf
if($LASTEXITCODE -ne 0){throw 'Experiment link failed'}
& "$DevkitPro/tools/bin/elf2nro.exe" $elf $candidate
if($LASTEXITCODE -ne 0){throw 'Experiment NRO conversion failed'}
& "$repo/scripts/verify-nro.ps1" -Path $candidate
& "$repo/scripts/embed-icon.ps1" -Nro $candidate -Icon "$repo/assets/BackupLoaderMKW.jpg" -Title 'BackupLoader Process Experiment'
& "$repo/scripts/verify-nro.ps1" -Path $candidate
Copy-Item -LiteralPath $candidate -Destination $nro -Force
Get-FileHash -LiteralPath $nro -Algorithm SHA256 | Format-List
Write-Output "[OK] Experimental process test: $nro"
