# SPDX-License-Identifier: MIT
# Copyright (c) HanSoloBlaster, afiaka87
param(
    [Parameter(Mandatory=$true)][string]$ToolboxDir,
    [Parameter(Mandatory=$true)][string]$UnsafeCompatPath,
    [Parameter(Mandatory=$true)][string]$TotkRomfs,
    [Parameter(Mandatory=$true)][string]$InputDirectory,
    [Parameter(Mandatory=$true)][string]$NativeSkeleton,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
foreach ($name in 'ToolboxDir','UnsafeCompatPath','TotkRomfs','InputDirectory','NativeSkeleton','OutputDirectory') {
    Set-Variable $name ([IO.Path]::GetFullPath((Get-Variable $name -ValueOnly)))
}
. (Join-Path $PSScriptRoot 'wolf_toolbox.ps1') -ToolboxDir $ToolboxDir -UnsafeCompatPath $UnsafeCompatPath -TotkRomfs $TotkRomfs
function Save-Skeleton($resource,[string]$path) {
    $resource.Save($path)
    $raw = [IO.File]::ReadAllBytes($path)
    if ([Text.Encoding]::ASCII.GetString($raw,0,4) -ne 'FRES') {
        if ($resource.Models.Count -ne 1 -or $resource.Models[0].VertexBuffers.Count -ne 0 -or
            $resource.SkeletalAnims.Count -ne 0) { throw 'Unexpected model writer signature failure' }
        $emptyPool = [BitConverter]::ToUInt32($raw,0)
        if ($emptyPool -lt 0xD0 -or $emptyPool+256 -gt $raw.Length -or
            @($raw[$emptyPool..($emptyPool+255)] | Where-Object { $_ -ne 0 }).Count) {
            throw 'Unknown skeleton signature corruption'
        }
        # The verified writer puts its empty GPU-pool offset over skeleton-only FRES signatures.
        [Text.Encoding]::ASCII.GetBytes('FRES').CopyTo($raw,0)
        [IO.File]::WriteAllBytes($path,$raw)
    }
    $type = Find-Type 'FirstPlugin.MeshCodec'
    $method = $type.GetMethods([Reflection.BindingFlags]'Public,NonPublic,Instance,Static') | Where-Object {
        $_.Name -eq 'CompressMeshCodec' -and $_.GetParameters().Count -eq 1 -and
        $_.GetParameters()[0].ParameterType.FullName -eq 'System.IO.Stream'
    } | Select-Object -First 1
    if (!$method) { throw 'Missing native model compressor' }
    $codec = [Activator]::CreateInstance($type,$true)
    $stream = [IO.File]::OpenRead($path)
    try { $bytes = $method.Invoke($codec,@($stream)) } finally { $stream.Dispose() }
    if ([Text.Encoding]::ASCII.GetString($bytes,0,4) -ne 'MCPK') { throw 'Invalid skeleton container' }
    [IO.File]::WriteAllBytes($path+'.mc',$bytes)
}

[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$native = Load-Resource $NativeSkeleton
$records = @()
foreach ($animal in 'Boar','Wolf') {
    $input = Join-Path $InputDirectory "Animal_${animal}_Animation.sbfres"
    $project = "BotwWolfLink_${animal}"
    $animation = Load-Resource $input
    if ($animation.Models.Count -ne 1 -or $animation.Models[0].Name -ne 'Skeleton') {
        throw "Unexpected original animation armature: $animal"
    }
    $clipsBefore = Clip-Signature $animation.SkeletalAnims
    foreach ($clip in $animation.MaterialAnims) {
        foreach ($material in $clip.MaterialAnimDataList) {
            foreach ($parameter in $material.ParamAnimInfos) {
                if ($parameter.Name -notin @('const_value0','const_value4')) {
                    throw "Unknown original material animation binding $($parameter.Name)"
                }
                $parameter.Name = 'p_'+$parameter.Name
            }
        }
    }
    $materialsBefore = Clip-Signature $animation.MaterialAnims
    $bonesBefore = Skeleton-Signature $animation.Models[0].Skeleton
    $boneNames = @($animation.Models[0].Skeleton.Bones | ForEach-Object Name)
    foreach ($clip in $animation.SkeletalAnims) {
        foreach ($track in $clip.BoneAnims) {
            if ($track.Name -notin $boneNames) { throw "Missing original bone $($track.Name)" }
        }
    }
    Native-Version $animation $native
    $animation.Name = $project
    $animation.Models.Clear(); $animation.ModelDict.Clear()
    $animationPath = Join-Path $OutputDirectory "$project.anim.bfres"
    $animation.Save($animationPath)
    $after = Load-Resource $animationPath
    if ((Clip-Signature $after.SkeletalAnims) -ne $clipsBefore) {
        throw "Original motion changed while converting $animal"
    }
    if ((Clip-Signature $after.MaterialAnims) -ne $materialsBefore) {
        throw "Original visibility or material curves changed while converting $animal"
    }
    $skeleton = Load-Resource $input
    Native-Version $skeleton $native
    $skeleton.Name = "$project.Skeleton"
    $skeleton.SkeletalAnims.Clear(); $skeleton.SkeletalAnimDict.Clear()
    $skeleton.MaterialAnims.Clear(); $skeleton.MaterialAnimDict.Clear()
    $skeletonPath = Join-Path $OutputDirectory "$project.Skeleton.bfres"
    Save-Skeleton $skeleton $skeletonPath
    $afterSkeleton = Load-Resource $skeletonPath
    if ((Skeleton-Signature $afterSkeleton.Models[0].Skeleton) -ne $bonesBefore) {
        throw "Original retargeting armature changed: $animal"
    }
    $records += [ordered]@{
        project=$project; source_sha256=(Get-FileHash -LiteralPath $input -Algorithm SHA256).Hash.ToLowerInvariant()
        clips=$after.SkeletalAnims.Count; materials=$after.MaterialAnims.Count; bones=$boneNames.Count
        clip_names=[string[]]@($after.SkeletalAnims | ForEach-Object Name)
        material_names=[string[]]@($after.MaterialAnims | ForEach-Object Name)
        animation_sha256=(Get-FileHash -LiteralPath $animationPath -Algorithm SHA256).Hash.ToLowerInvariant()
        skeleton_sha256=(Get-FileHash -LiteralPath ($skeletonPath+'.mc') -Algorithm SHA256).Hash.ToLowerInvariant()
        animation_bytes=(Get-Item -LiteralPath $animationPath).Length
        skeleton_bytes=(Get-Item -LiteralPath $skeletonPath).Length
    }
    Write-Host "Verified $project clips=$($after.SkeletalAnims.Count) bones=$($boneNames.Count)"
}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'animation-receipt.json'),(Json $records))
