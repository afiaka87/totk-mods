# SPDX-License-Identifier: MIT
# Copyright (c) HanSoloBlaster, afiaka87
# Writes a renamed 1.4.3 Champion model copy, remapping two o_expression_post_normal values that 1.4.x lacks.
param(
    [Parameter(Mandatory=$true)][string]$ToolboxDir,
    [Parameter(Mandatory=$true)][string]$UnsafeCompatPath,
    [Parameter(Mandatory=$true)][string]$TotkRomfs,
    [Parameter(Mandatory=$true)][string]$SourceModel,
    [Parameter(Mandatory=$true)][string]$Project,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
foreach ($name in 'ToolboxDir','UnsafeCompatPath','TotkRomfs','SourceModel','OutputDirectory') {
    Set-Variable $name ([IO.Path]::GetFullPath((Get-Variable $name -ValueOnly)))
}
. (Join-Path $PSScriptRoot 'wolf_toolbox.ps1') -ToolboxDir $ToolboxDir -UnsafeCompatPath $UnsafeCompatPath -TotkRomfs $TotkRomfs

$option = 'o_expression_post_normal'
$remap = @{ '1190662607' = '3610888425'; '3916162606' = '423127447' }

function Hash-Bytes([byte[]]$bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Geometry-Signature($model) {
    $vertices = @($model.VertexBuffers | ForEach-Object {
        [ordered]@{ count=$_.VertexCount; skin=$_.VertexSkinCount; attributes=@($_.Attributes)
            buffers=@($_.Buffers | ForEach-Object { [ordered]@{stride=$_.Stride; data=(Hash-Bytes $_.Data)} })
        }
    })
    $shapes = @($model.Shapes | ForEach-Object {
        [ordered]@{name=$_.Name; skin=$_.VertexSkinCount; vertices=$_.VertexBufferIndex
            bone=$_.BoneIndex; material=$_.MaterialIndex; bones=$_.SkinBoneIndices
            boxes=$_.SubMeshBoundings; radii=$_.RadiusArray
            meshes=@($_.Meshes | ForEach-Object {
                [ordered]@{data=(Hash-Bytes $_.Data); first=$_.FirstVertex
                    type=$_.PrimitiveType; format=$_.IndexFormat; subMeshes=$_.SubMeshes}
            })
        }
    })
    Json ([ordered]@{vertices=$vertices; shapes=$shapes})
}
function Material-Signature($material) {
    Json ([ordered]@{name=$material.Name; shader=$material.ShaderAssign
        samplers=$material.Samplers; textures=$material.TextureRefs
        parameters=(Hash-Bytes $material.ShaderParamData); render=$material.RenderInfos})
}
function Option-Index($material) {
    $dict = $material.ShaderAssign.ShaderOptionDict
    for ($i=0; $i -lt $dict.Count; $i++) { if ($dict.GetKey($i) -ceq $option) { return $i } }
    return -1
}

[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$source = Load-Resource $SourceModel
if ($source.Models.Count -ne 1) { throw 'Expected one model' }
$model = $source.Models[0]
$geometry = Geometry-Signature $model
$skeleton = Skeleton-Signature $model.Skeleton
$originals = @($model.Materials | ForEach-Object { Material-Signature $_ })
$records = @()
foreach ($material in $model.Materials) {
    $index = Option-Index $material
    if ($index -lt 0) { continue }
    $value = $material.ShaderAssign.ShaderOptions[$index]
    if (!$remap.ContainsKey($value)) { throw "Unexpected $option value $value on $($material.Name)" }
    $material.ShaderAssign.ShaderOptions[$index] = $remap[$value]
    $records += [ordered]@{material=$material.Name; from=$value; to=$remap[$value]}
}
if (!$records.Count) { throw "No material sets $option" }

$source.Name = "$Project.$Project"
$model.Name = $Project
$source.ModelDict.Clear(); $source.ModelDict.Add($Project)
$out = Join-Path $OutputDirectory "$Project.$Project.bfres"
$source.Save($out)
# A geometry block not on a 4096-byte boundary draws nothing; saving the reloaded file aligns it, bytes unchanged.
(Load-Resource $out).Save($out)
$saved = [IO.File]::ReadAllBytes($out)
$bufferInfo = [BitConverter]::ToInt64($saved,0xB0)
$bufferSize = [BitConverter]::ToUInt32($saved,$bufferInfo+4)
$bufferStart = [BitConverter]::ToInt64($saved,$bufferInfo+8)
if ($bufferStart % 4096 -or $bufferSize % 4096) { throw 'Geometry block is not 4096-byte aligned' }
$codecType = Find-Type 'FirstPlugin.MeshCodec'
$codec = [Activator]::CreateInstance($codecType,$true)
$compress = $codecType.GetMethods([Reflection.BindingFlags]'Public,NonPublic,Instance,Static') | Where-Object {
    $_.Name -eq 'CompressMeshCodec' -and $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.FullName -eq 'System.IO.Stream'
} | Select-Object -First 1
if (!$compress) { throw 'Missing model compressor' }
$stream = [IO.File]::OpenRead($out)
try { $encoded = $compress.Invoke($codec,@($stream)) } finally { $stream.Dispose() }
if ([Text.Encoding]::ASCII.GetString($encoded,0,4) -ne 'MCPK') { throw 'Invalid model container' }
[IO.File]::WriteAllBytes($out+'.mc',$encoded)

$after = Load-Resource $out
if ($after.Name -cne "$Project.$Project" -or $after.Models[0].Name -cne $Project) { throw 'Model name did not survive' }
if ((Geometry-Signature $after.Models[0]) -ne $geometry) { throw 'Geometry or bounds changed' }
if ((Skeleton-Signature $after.Models[0].Skeleton) -ne $skeleton) { throw 'Armature changed' }
for ($i=0; $i -lt $model.Materials.Count; $i++) {
    $material = $after.Models[0].Materials[$i]
    $index = Option-Index $material
    if ($index -ge 0) {
        $value = $material.ShaderAssign.ShaderOptions[$index]
        $original = ($remap.GetEnumerator() | Where-Object Value -eq $value | Select-Object -First 1).Key
        if (!$original) { throw "Saved $option value $value on $($material.Name) is not a 1.4.x value" }
        $material.ShaderAssign.ShaderOptions[$index] = $original
    }
    if ((Material-Signature $material) -ne $originals[$i]) { throw "Material changed beyond $option`: $($material.Name)" }
}
$receipt = [ordered]@{project=$Project; option=$option; remapped=$records
    source_sha256=(Get-FileHash -LiteralPath $SourceModel).Hash.ToLowerInvariant()
    model_sha256=(Get-FileHash -LiteralPath ($out+'.mc')).Hash.ToLowerInvariant()
    raw_sha256=(Get-FileHash -LiteralPath $out).Hash.ToLowerInvariant()
    raw_bytes=(Get-Item -LiteralPath $out).Length; compressed_bytes=$encoded.Length}
[IO.File]::WriteAllText((Join-Path $OutputDirectory "$Project.receipt.json"),(Json $receipt))
Write-Host "$Project`: $($records.Count) materials remapped; geometry, armature and other material inputs unchanged"
