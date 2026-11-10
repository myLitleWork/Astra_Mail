#Requires -Version 5.1
[CmdletBinding()]
param([string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot), [switch]$SkipVersionBump, $InputState)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'AstraMailRelease.ps1')
if ($null -eq $InputState) { $InputState = Get-AstraMailInputs $ProjectRoot }
$path = Join-Path $ProjectRoot 'app-version.json'
$value = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
if ($value.productName -ne 'AstraMail') { throw 'Invalid release product' }
foreach ($part in @('major','minor','patch')) {
    if ([string]$value.$part -notmatch '^\d+$' -or [int]$value.$part -gt 65535) { throw "Invalid version component: $part" }
}
$previous = $value.PSObject.Properties['releaseInputSha256']
if ($null -ne $previous -and $previous.Value -ne $InputState.InputSha256 -and -not $SkipVersionBump) {
    $value.patch = [int]$value.patch+1
    if ($value.patch -gt 65535) { throw 'Patch version exhausted' }
}
$value | Add-Member NoteProperty releaseFingerprintFormat 1 -Force
$value | Add-Member NoteProperty releaseInputSha256 $InputState.InputSha256 -Force
Write-AstraMailJson $path $value
$read = Resolve-AstraMailReleaseHelper $ProjectRoot 'Get-CmakeAppVersionNames.ps1'
$info = & $read -ProjectRoot $ProjectRoot -ProductName AstraMail -VersionFileName app-version.json
$info | Add-Member NoteProperty SourceInputSha256 $InputState.InputSha256
$info
