#Requires -Version 5.1
[CmdletBinding()]
param([Parameter(Mandatory)][string]$PackageDirectory,
      [Parameter(Mandatory)][string]$ExpectedVersion,
      [Parameter(Mandatory)][string]$ExpectedInputSha256)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'AstraMailRelease.ps1')
$base = (Resolve-Path -LiteralPath $PackageDirectory).Path
$manifest = Get-Content -LiteralPath (Join-Path $base 'package-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
if ($manifest.product -ne 'AstraMail' -or $manifest.version -ne $ExpectedVersion -or
    $manifest.source.inputSha256 -ne $ExpectedInputSha256 -or $manifest.configuration -ne 'Release' -or
    $manifest.architecture -ne 'x86_64' -or $manifest.containsLocalSettings -ne $false -or
    $manifest.validation.nativeChecks -ne 474 -or $manifest.validation.csvSampleContacts -ne 3 -or
    $manifest.validation.packagedUiCsvImport -ne $true -or
    $manifest.validation.isolatedInstaller -ne $true -or
    $manifest.validation.installedUiCsvImport -ne $true) { throw 'Package provenance/validation mismatch' }
$required = @('AstraMail','Run-AstraMail.sh','Verify-Package.sh','contacts-import-example.csv','version.txt','scripts/install_astramail.sh',
              'lib/libsodium.so.18','licenses/libsodium.txt','licenses/gcc-runtime.txt')
$declared = @{}
foreach ($file in $manifest.files) {
    $rel = [string]$file.path
    if ($rel -notmatch '^[^\\:]+$' -or $rel -match '(^|/)\.{1,2}(/|$)' -or $rel.StartsWith('/')) { throw "Unsafe package path: $rel" }
    if ($declared.ContainsKey($rel)) { throw 'Duplicate package path' }
    $declared[$rel] = [string]$file.sha256
    $full = Join-Path $base $rel
    if (!(Test-Path -LiteralPath $full -PathType Leaf) -or
        (Get-AstraMailSha256 $full) -cne $file.sha256) { throw "Package file mismatch: $rel" }
}
foreach ($rel in $required) { if (!$declared.ContainsKey($rel)) { throw "Missing package payload: $rel" } }
$project = Split-Path -Parent $PSScriptRoot
$installer = 'scripts/install_astramail.sh'
if ($declared[$installer] -cne (Get-AstraMailSha256 (Join-Path $project $installer))) { throw 'Installer differs from project source' }
$tutorialRoot = Join-Path $project 'tutorials'
foreach ($file in Get-ChildItem -LiteralPath $tutorialRoot -Recurse -File -Force) {
    if ($file.Extension -notmatch '^\.(md|html?|doc|css)$') { continue }
    $rel = 'tutorials/'+$file.FullName.Substring($tutorialRoot.Length+1).Replace('\','/')
    if (!$declared.ContainsKey($rel) -or $declared[$rel] -cne (Get-AstraMailSha256 $file.FullName)) {
        throw "Missing or changed operator document: $rel"
    }
}
foreach ($guide in @('содержание.html','установка-и-обновление.html','первый-запуск-и-thunderbird.html','контакты-и-импорт.html','работа-с-письмами.html','настройки.html','фоновый-режим-и-связь.html','данные-и-восстановление.html','ошибки-и-диагностика.html','встроенная-справка.html','оформление.css')) {
    if (!$declared.ContainsKey('tutorials/'+$guide)) { throw "Missing offline guide: $guide" }
}
if ((Get-Content -LiteralPath (Join-Path $base 'version.txt') -Raw).Trim() -ne $ExpectedVersion) { throw 'version.txt mismatch' }
$binary = [IO.File]::OpenRead((Join-Path $base 'AstraMail'))
try { $head = New-Object byte[] 5; if ($binary.Read($head,0,5) -ne 5 -or [BitConverter]::ToString($head) -ne '7F-45-4C-46-02') { throw 'Expected 64-bit ELF executable' } }
finally { $binary.Dispose() }
$checksums = @{}
foreach ($line in Get-Content -LiteralPath (Join-Path $base 'package-checksums.sha256') -Encoding UTF8) {
    if ($line -notmatch '^([a-f0-9]{64})  (.+)$') { throw 'Malformed checksum file' }
    if ($checksums.ContainsKey($Matches[2])) { throw 'Duplicate checksum path' }
    $checksums[$Matches[2]] = $Matches[1]
}
$expected = @($declared.Keys) + @('package-manifest.json')
if ($checksums.Count -ne $expected.Count) { throw 'Unexpected checksum coverage' }
foreach ($rel in $expected) {
    if (!$checksums.ContainsKey($rel) -or
        (Get-AstraMailSha256 (Join-Path $base $rel)) -cne $checksums[$rel]) { throw "Checksum mismatch: $rel" }
}
$actual = @(Get-ChildItem -LiteralPath $base -Recurse -File -Force)
if ($actual.Count -ne $expected.Count+1) { throw 'Unexpected files in clean package' }
Write-Host "Verified AstraMail $ExpectedVersion, $($actual.Count) package files."
