# Installs the Claude Usage Widget alert hooks.
#  1. Copies widget_signal.py/.bat into ~/.claude/hooks (a stable location that
#     survives moving this repo).
#  2. Merges four command hooks into ~/.claude/settings.json, preserving any
#     hooks/keys already there and overwriting only our four events.
$ErrorActionPreference = 'Stop'

$src    = $PSScriptRoot
$dstDir = Join-Path $env:USERPROFILE '.claude\hooks'
New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
Copy-Item -Force (Join-Path $src 'widget_signal.py')  $dstDir
Copy-Item -Force (Join-Path $src 'widget_signal.bat') $dstDir
$bat = Join-Path $dstDir 'widget_signal.bat'

$settingsPath = Join-Path $env:USERPROFILE '.claude\settings.json'
if (Test-Path $settingsPath) {
    $json = Get-Content $settingsPath -Raw | ConvertFrom-Json
} else {
    $json = [pscustomobject]@{}
}

function New-CmdHook([string]$action) {
    @{ hooks = @(@{ type = 'command'; command = "`"$bat`" $action" }) }
}

$ours = [ordered]@{
    Notification     = @(New-CmdHook 'ask')    # needs input  -> flashing border
    Stop             = @(New-CmdHook 'done')   # finished work -> solid border
    UserPromptSubmit = @(New-CmdHook 'clear')  # you responded -> clear
    SessionEnd       = @(New-CmdHook 'clear')  # session gone  -> clear
}

if (($json.PSObject.Properties.Name -contains 'hooks') -and $json.hooks) {
    foreach ($k in $ours.Keys) {
        $json.hooks | Add-Member -NotePropertyName $k -NotePropertyValue $ours[$k] -Force
    }
} else {
    $json | Add-Member -NotePropertyName 'hooks' -NotePropertyValue ([pscustomobject]$ours) -Force
}

$out = $json | ConvertTo-Json -Depth 12
[System.IO.File]::WriteAllText($settingsPath, $out, [System.Text.UTF8Encoding]::new($false))
Write-Host "  Installed widget alert hooks into $settingsPath"
