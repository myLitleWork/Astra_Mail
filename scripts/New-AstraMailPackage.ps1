#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackageDirectory,
      [Parameter(Mandatory)][string]$ExpectedVersion,
      [Parameter(Mandatory)][string]$ExpectedInputSha256,
      [string]$CloudRoot, [switch]$SkipCloudPublish)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'AstraMailRelease.ps1')
& (Join-Path $PSScriptRoot 'Test-AstraMailPackage.ps1') -PackageDirectory $PackageDirectory -ExpectedVersion $ExpectedVersion -ExpectedInputSha256 $ExpectedInputSha256
$publisher = Resolve-AstraMailReleaseHelper $project 'Publish-ReleasePackageIfConfigured.ps1'
$published = & $publisher -PackageDirectory $PackageDirectory -ProductName AstraMail -ReleaseId $ExpectedVersion -ZipName "AstraMail-$ExpectedVersion.zip" -CloudRoot $CloudRoot -StartPath $project -SkipCloudPublish:$SkipCloudPublish -UseForwardSlashEntryNames
[pscustomobject]@{
    Product = 'AstraMail'; Version = $ExpectedVersion; FileVersion = $ExpectedVersion
    PackageDirectory = (Resolve-Path -LiteralPath $PackageDirectory).Path
    PackagedExe = Join-Path $PackageDirectory 'AstraMail'
    CloudPublished = ($null -ne $published)
    ZipPath = if ($published) { $published.ZipPath } else { $null }
    Sha256Path = if ($published) { $published.Sha256Path } else { $null }
    Sha256 = if ($published) { $published.Sha256 } else { $null }
}
