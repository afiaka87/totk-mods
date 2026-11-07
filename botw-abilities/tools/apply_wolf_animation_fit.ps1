# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
# Rewrites Wolf Link's animation file from the records made by compact_wolf_animations.py.
param(
    [Parameter(Mandatory)][string]$ToolboxInit,
    [Parameter(Mandatory)][string]$Source,
    [Parameter(Mandatory)][string]$Fit,
    [Parameter(Mandatory)][string]$Output
)
$ErrorActionPreference = 'Stop'
. $ToolboxInit
$invariant = [Globalization.CultureInfo]::InvariantCulture
function Parse-Float([string]$text) { [float]::Parse($text, $invariant) }

function Set-BaseData($bone, [string[]]$fields) {
    $data = $bone.BaseData
    $translate = $data.Translate; $rotate = $data.Rotate; $scale = $data.Scale
    $translate.X = Parse-Float $fields[4]; $translate.Y = Parse-Float $fields[5]; $translate.Z = Parse-Float $fields[6]
    $rotate.X = Parse-Float $fields[7]; $rotate.Y = Parse-Float $fields[8]; $rotate.Z = Parse-Float $fields[9]
    $scale.X = Parse-Float $fields[10]; $scale.Y = Parse-Float $fields[11]; $scale.Z = Parse-Float $fields[12]
    $data.Translate = $translate; $data.Rotate = $rotate; $data.Scale = $scale
    $bone.BaseData = $data
}

function New-Curve([string[]]$fields, $sample) {
    $curve = [Activator]::CreateInstance($sample.GetType())
    $frames = [float[]]($fields[7].Split(',') | ForEach-Object { [float]$_ })
    $values = $fields[8].Split(',')
    $keys = New-Object 'float[,]' $frames.Length, 4
    for ($row = 0; $row -lt $frames.Length; $row++) {
        for ($column = 0; $column -lt 4; $column++) { $keys[$row, $column] = [float]$values[$row * 4 + $column] }
    }
    $curve.CurveType = $sample.CurveType
    $curve.FrameType = [Enum]::ToObject($sample.FrameType.GetType(), [int]$fields[2])
    $curve.KeyType = [Enum]::ToObject($sample.KeyType.GetType(), [int]$fields[3])
    $curve.AnimDataOffset = [uint32]$fields[1]
    $curve.StartFrame = $frames[0]
    $curve.EndFrame = $frames[$frames.Length - 1]
    $curve.Scale = Parse-Float $fields[4]
    $curve.Offset = [float](Parse-Float $fields[5])
    $curve.Delta = Parse-Float $fields[6]
    $curve.Frames = $frames
    $curve.Keys = $keys
    $curve
}

$resource = (Load-Bfres $Source).resFile
$byName = @{}
foreach ($clip in $resource.SkeletalAnims) { $byName[$clip.Name] = $clip }
$sample = $byName['Appear'].BoneAnims[3].Curves[0]
$resource.SkeletalAnims.Clear()
$resource.SkeletalAnimDict.Clear()
$clip = $null
$bone = $null
foreach ($line in [IO.File]::ReadLines($Fit)) {
    $fields = $line.Split("`t")
    switch ($fields[0]) {
        'clip' {
            $clip = $byName[$fields[2]]
            if (-not $clip) { throw "Missing template clip: $($fields[2])" }
            $clip.Name = $fields[1]
            $clip.FrameCount = [int]$fields[3]
            $clip.BakedSize = [uint32]$fields[4]
            if ($fields[5] -eq '0') { $clip.UserDatas.Clear(); $clip.UserDataDict.Clear() }
            $resource.SkeletalAnims.Add($clip)
            $resource.SkeletalAnimDict.Add($clip.Name)
        }
        'bone' {
            $bone = $clip.BoneAnims[[int]$fields[1]]
            $bone.Curves.Clear()
            $bone.FlagsCurve = [Enum]::ToObject($bone.FlagsCurve.GetType(), [int]$fields[2])
            $bone.FlagsBase = [Enum]::ToObject($bone.FlagsBase.GetType(), [int]$fields[3])
            Set-BaseData $bone $fields
        }
        'curve' { $bone.Curves.Add((New-Curve $fields $sample)) }
        default { throw "Unknown record: $($fields[0])" }
    }
}
$resource.Save($Output)
$check = (Load-Bfres $Output).resFile
[IO.File]::WriteAllText([IO.Path]::ChangeExtension($Output, '.clips.json'),
    [Newtonsoft.Json.JsonConvert]::SerializeObject($check.SkeletalAnims))
"$($check.SkeletalAnims.Count) clips, $((Get-Item $Output).Length) bytes"
