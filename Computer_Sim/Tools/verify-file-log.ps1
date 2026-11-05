$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $projectRoot 'MDK-ARM'
$checkRoot = Join-Path $projectRoot 'tmp/file-log-build'
$linkCheckRoot = '../tmp/file-log-build'
$compiler = 'D:/keil5/ARM/ARMCC/bin/armcc.exe'
$linker = 'D:/keil5/ARM/ARMCC/bin/armlink.exe'
$fromElf = 'D:/keil5/ARM/ARMCC/bin/fromelf.exe'
New-Item -ItemType Directory -Path $checkRoot -Force | Out-Null
[xml]$project = Get-Content -LiteralPath (Join-Path $buildRoot 'Computer_Sim.uvprojx') -Raw
$outputDirectory = [string]$project.Project.Targets.Target.TargetOption.TargetCommonOption.OutputDirectory
$controls = $project.Project.Targets.Target.TargetOption.TargetArmAds.Cads.VariousControls
$options = @('--c99', '-c', '--cpu=Cortex-M4.fp.sp', '--apcs=interwork', '--split_sections', '-g', '-O0')
foreach($definition in $controls.Define.Split(',')) { $options += "-D$definition" }
foreach($include in $controls.IncludePath.Split(';')) { $options += @('-I', $include) }
$sources = @('../Core/Src/main.c', '../UI/DesktopUI.c', '../UI/AudioPlayer.c', '../UI/DrawingApp.c', '../UI/SystemLog.c')
Push-Location $buildRoot
try {
    foreach($source in $sources) {
        $name = [IO.Path]::GetFileNameWithoutExtension($source).ToLowerInvariant()
        & $compiler @options $source '-o' (Join-Path $checkRoot "$name.o")
        if($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
    }
    $linkInput = [IO.File]::ReadAllText((Join-Path $buildRoot ($outputDirectory + 'Computer_Sim.lnp')))
    foreach($source in $sources) {
        $name = [IO.Path]::GetFileNameWithoutExtension($source).ToLowerInvariant()
        if($name -eq 'systemlog') { continue }
        $linkInput = $linkInput.Replace("$outputDirectory$name.o", "$linkCheckRoot/$name.o")
    }
    $linkInput = '"' + "$linkCheckRoot/systemlog.o" + '"' + "`r`n" + $linkInput
    $linkInput = $linkInput.Replace('--list "Computer_Sim.map"', '--list "' + "$linkCheckRoot/Computer_Sim.map" + '"')
    $linkInput = $linkInput.Replace("-o ${outputDirectory}Computer_Sim.axf", '-o "' + "$linkCheckRoot/Computer_Sim.axf" + '"')
    $linkFile = Join-Path $checkRoot 'file-log.lnp'
    [IO.File]::WriteAllText($linkFile, $linkInput, [Text.Encoding]::ASCII)
    & $linker '--via' $linkFile
    if($LASTEXITCODE -ne 0) { throw 'Link failed' }
    & $fromElf '--i32combined' '--output' (Join-Path $checkRoot 'Computer_Sim.hex') (Join-Path $checkRoot 'Computer_Sim.axf')
    if($LASTEXITCODE -ne 0) { throw 'HEX generation failed' }
    Write-Output "Build passed: $checkRoot/Computer_Sim.hex"
}
finally { Pop-Location }
