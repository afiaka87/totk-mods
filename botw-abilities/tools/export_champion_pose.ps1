# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
param(
    [Parameter(Mandatory = $true)][string]$ToolboxDir,
    [Parameter(Mandatory = $true)][string]$InputBfres,
    [Parameter(Mandatory = $true)][string]$OutputJson
)
$ErrorActionPreference = 'Stop'
$lib = Join-Path $ToolboxDir 'Lib'
foreach ($name in @('Syroot.Maths', 'Syroot.BinaryData', 'Syroot.NintenTools.NSW.Bntx', 'Syroot.NintenTools.NSW.Bfres', 'Newtonsoft.Json')) {
    [Reflection.Assembly]::LoadFrom((Join-Path $lib "$name.dll")) | Out-Null
}
Add-Type -ReferencedAssemblies @("$lib/Syroot.NintenTools.NSW.Bfres.dll", "$lib/Newtonsoft.Json.dll", 'System.Core') -TypeDefinition @'
using System;
using System.IO;
using System.Linq;
using Syroot.NintenTools.NSW.Bfres;
using Newtonsoft.Json;

public static class ChampionPoseExport {
    public static void Run(string input, string output) {
        var res = new ResFile(input);
        if (res.Models.Count != 1) throw new InvalidDataException("Expected one animation skeleton");
        var clips = res.SkeletalAnims.Where(x => x.Name == "Nml_Wait_To_Squat_Wait" ||
            x.Name == "Squat_Wait_FootL" || x.Name == "Squat_Wait_FootR").ToArray();
        var settings = new JsonSerializerSettings { ReferenceLoopHandling = ReferenceLoopHandling.Ignore };
        File.WriteAllText(output, JsonConvert.SerializeObject(
            new { Skeleton = res.Models[0].Skeleton, Clips = clips }, Formatting.Indented, settings));
    }
}
'@
[ChampionPoseExport]::Run($InputBfres, $OutputJson)
