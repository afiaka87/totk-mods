# SPDX-License-Identifier: MIT
# Copyright (c) Clay Mullis
param([string]$Toolbox, [string]$Tasks)
$ErrorActionPreference = 'Stop'
$library = Join-Path $Toolbox 'Lib'
[AppDomain]::CurrentDomain.add_AssemblyResolve({
    param($sender, $event)
    $file = Join-Path $library (([Reflection.AssemblyName]$event.Name).Name + '.dll')
    if (Test-Path -LiteralPath $file) { return [Reflection.Assembly]::LoadFrom($file) }
    return $null
})
[void][Reflection.Assembly]::LoadFrom((Join-Path $library 'Syroot.NintenTools.NSW.Bntx.dll'))
foreach ($task in (Get-Content -LiteralPath $Tasks -Raw | ConvertFrom-Json)) {
    $bank = New-Object Syroot.NintenTools.NSW.Bntx.BntxFile ([string]$task.input)
    if ($bank.Textures.Count -ne 1) { throw 'Expected one inventory texture' }
    $bank.Name = [string]$task.actor
    $bank.Textures[0].Name = [string]$task.actor
    $bank.Textures[0].Path = ''
    $bank.Save([string]$task.output)
    $check = New-Object Syroot.NintenTools.NSW.Bntx.BntxFile ([string]$task.output)
    if ($check.Name -ne $task.actor -or $check.Textures[0].Name -ne $task.actor) {
        throw 'Inventory texture rename failed'
    }
}
