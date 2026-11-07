# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
param(
    [Parameter(Mandatory=$true)][string]$ToolboxDir,
    [Parameter(Mandatory=$true)][string]$UnsafeCompatPath,
    [Parameter(Mandatory=$true)][string]$TotkRomfs,
    [Parameter(Mandatory=$true)][string]$OriginalModel,
    [Parameter(Mandatory=$true)][string]$NativeBody,
    [Parameter(Mandatory=$true)][string]$NativeLink,
    [Parameter(Mandatory=$true)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
foreach ($name in 'ToolboxDir','UnsafeCompatPath','TotkRomfs','OriginalModel','NativeBody','NativeLink','OutputDirectory') {
    Set-Variable $name ([IO.Path]::GetFullPath((Get-Variable $name -ValueOnly)))
}
. (Join-Path $PSScriptRoot 'wolf_toolbox.ps1') -ToolboxDir $ToolboxDir -UnsafeCompatPath $UnsafeCompatPath -TotkRomfs $TotkRomfs

function Hash-Bytes([byte[]]$bytes) {
    $sha = [Security.Cryptography.SHA256]::Create()
    try { ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
}
function Geometry-Signature($model) {
    $vertices = @($model.VertexBuffers | ForEach-Object {
        [ordered]@{ count=$_.VertexCount; skin=$_.VertexSkinCount; attributes=@($_.Attributes | Where-Object Name -ne '_u1')
            buffers=@($_.Buffers | ForEach-Object {
                [ordered]@{stride=$_.Stride; data=(Hash-Bytes $_.Data)}
            })
        }
    })
    $shapes = @($model.Shapes | ForEach-Object {
        [ordered]@{name=$_.Name; skin=$_.VertexSkinCount; vertices=$_.VertexBufferIndex
            bone=$_.BoneIndex; material=$_.MaterialIndex; bones=$_.SkinBoneIndices
            boxes=$_.SubMeshBoundings; maxRadius=($_.RadiusArray | Measure-Object -Maximum).Maximum
            meshes=@($_.Meshes | ForEach-Object {
                [ordered]@{data=(Hash-Bytes $_.Data); first=$_.FirstVertex
                    type=$_.PrimitiveType; format=$_.IndexFormat; subMeshes=$_.SubMeshes}
            })
        }
    })
    Json ([ordered]@{vertices=$vertices; shapes=$shapes})
}
function Key-Index($dictionary,[string]$name) {
    for ($i=0; $i -lt $dictionary.Count; $i++) {
        if ($dictionary.GetKey($i) -ceq $name) { return $i }
    }
    return -1
}
function Copy-MaterialInputs($source,$target) {
    if ($source.SamplerDict.Count -ne $target.SamplerDict.Count) { throw 'Sampler counts differ' }
    for ($i=0; $i -lt $target.Samplers.Count; $i++) {
        $key = $target.SamplerDict.GetKey($i)
        $index = Key-Index $source.SamplerDict $key
        if ($index -lt 0) { throw "Missing original sampler $key" }
        $target.Samplers[$i] = $source.Samplers[$index]
        $target.TextureRefs[$i] = 'BotwWolfLink_'+$source.TextureRefs[$index]
    }
    foreach ($parameter in $target.ShaderParams) {
        if ($parameter.Name -notmatch '^p_(const_|tex_srt)') { continue }
        $name = $parameter.Name.Substring(2)
        $original = $source.ShaderParams | Where-Object Name -eq $name
        if (!$original -or $original.Type -ne $parameter.Type -or $original.DataSize -ne $parameter.DataSize) {
            throw "Missing compatible original parameter $name"
        }
        [Array]::Copy($source.ShaderParamData,$original.DataOffset,
            $target.ShaderParamData,$parameter.DataOffset,$parameter.DataSize)
    }
}
function Material-Signature($material) {
    Json ([ordered]@{name=$material.Name; shader=$material.ShaderAssign
        samplers=$material.Samplers; textures=$material.TextureRefs
        parameters=(Hash-Bytes $material.ShaderParamData); render=$material.RenderInfos})
}

[IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$source = Load-Resource $OriginalModel
$model = $source.Models[0]
if ($source.Models.Count -ne 1 -or $model.Skeleton.Bones.Count -ne 41 -or $model.Shapes.Count -ne 4) {
    throw 'Unexpected original Wolf Link model'
}
$geometry = Geometry-Signature $model
$skeleton = Skeleton-Signature $model.Skeleton
foreach ($vertices in $model.VertexBuffers) {
    if ('_u1' -in $vertices.Attributes.Name) { throw 'Original UV1 contract changed' }
    $uv = $vertices.Attributes | Where-Object Name -eq '_u0' | Select-Object -First 1
    if (!$uv) { throw 'Native material requires a UV0 source for UV1' }
    $alias = [Activator]::CreateInstance($uv.GetType())
    $alias.Name = '_u1'; $alias.BufferIndex = $uv.BufferIndex
    $alias.Offset = $uv.Offset; $alias.Format = $uv.Format
    $vertices.Attributes.Add($alias); $vertices.AttributeDict.Add('_u1')
}
$records = @()
for ($i=0; $i -lt $model.Materials.Count; $i++) {
    $original = $model.Materials[$i]
    $eye = $original.Name -in @('Mt_Eyeball_L','Mt_Eyeball_R')
    if (!$eye -and $original.Name -ne 'Mt_Body') { throw 'Unexpected original material' }
    $donorPath = if ($eye) { $NativeLink } else { $NativeBody }
    $donor = Load-Resource $donorPath
    $donorName = if ($eye) { $original.Name } else { 'Mt_Armband' }
    $material = $donor.Models[0].Materials | Where-Object Name -eq $donorName
    if (!$material) { throw "Missing donor material $($original.Name)" }
    $shader = Json $material.ShaderAssign
    $material.Name = $original.Name
    Copy-MaterialInputs $original $material
    if ((Json $material.ShaderAssign) -ne $shader) { throw 'Native shader permutation changed' }
    $model.Materials[$i] = $material
    $records += [ordered]@{material=$material.Name; donor=[IO.Path]::GetFileName($donorPath)
        donor_material=$donorName; shader_unchanged=$true
        donor_sha256=(Get-FileHash -LiteralPath $donorPath).Hash.ToLowerInvariant()
        textures=@($material.TextureRefs)}
}
$project = 'BotwWolfLink'
$source.Name = "$project.$project"
$model.Name = $project
$source.ModelDict.Clear(); $source.ModelDict.Add($project)
$source.ExternalFiles.Clear(); $source.ExternalFileDict.Clear()
Native-Version $source $donor
$out = Join-Path $OutputDirectory "$project.$project.bfres"
$source.Save($out)
# The game shows nothing for a model whose geometry block is not on a 4096-byte boundary. A saved
# conversion is not aligned; saving the reloaded file aligns the block and leaves its bytes alone.
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
if (!$compress) { throw 'Missing native model compressor' }
$stream = [IO.File]::OpenRead($out)
try { $encoded = $compress.Invoke($codec,@($stream)) } finally { $stream.Dispose() }
if ([Text.Encoding]::ASCII.GetString($encoded,0,4) -ne 'MCPK') { throw 'Invalid native model container' }
[IO.File]::WriteAllBytes($out+'.mc',$encoded)
$after = Load-Resource $out
foreach ($vertices in $after.Models[0].VertexBuffers) {
    $uv = $vertices.Attributes | Where-Object Name -eq '_u0'
    $alias = $vertices.Attributes | Where-Object Name -eq '_u1'
    if (!$alias -or $alias.BufferIndex -ne $uv.BufferIndex -or $alias.Offset -ne $uv.Offset -or $alias.Format -ne $uv.Format) {
        throw 'UV1 alias did not survive serialization'
    }
}
foreach ($shape in $after.Models[0].Shapes) {
    $count = if ($shape.SkinBoneIndices.Count) { $shape.SkinBoneIndices.Count } else { $shape.Meshes.Count }
    if ($shape.BoundingRadiusList.Count -ne $count) { throw 'Incomplete native culling spheres' }
    foreach ($sphere in $shape.BoundingRadiusList) {
        if ($sphere.X -ne 0 -or $sphere.Y -ne 0 -or $sphere.Z -ne 0 -or $sphere.W -ne $shape.RadiusArray[0]) {
            throw 'Unexpected native culling sphere conversion'
        }
    }
}
$afterGeometry = Geometry-Signature $after.Models[0]
if ($afterGeometry -ne $geometry) {
    [IO.File]::WriteAllText($out+'.before.json',$geometry)
    [IO.File]::WriteAllText($out+'.after.json',$afterGeometry)
    throw 'Original geometry or bounds changed'
}
if ((Skeleton-Signature $after.Models[0].Skeleton) -ne $skeleton) { throw 'Original armature changed' }
for ($i=0; $i -lt $model.Materials.Count; $i++) {
    if ((Material-Signature $model.Materials[$i]) -ne (Material-Signature $after.Models[0].Materials[$i])) {
        throw "Material serialization changed $($model.Materials[$i].Name)"
    }
}
$receipt = [ordered]@{project=$project; bones=41; shapes=4; materials=$records
    source_sha256=(Get-FileHash -LiteralPath $OriginalModel).Hash.ToLowerInvariant()
    model_sha256=(Get-FileHash -LiteralPath ($out+'.mc')).Hash.ToLowerInvariant()
    raw_sha256=(Get-FileHash -LiteralPath $out).Hash.ToLowerInvariant()
    raw_bytes=(Get-Item -LiteralPath $out).Length; compressed_bytes=$encoded.Length
    material_status='Native adapter candidate; rendered parity is not verified'}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'model-receipt.json'),(Json $receipt))
Write-Host 'Verified original Wolf Link geometry, bounds, armature and serialized material inputs'
