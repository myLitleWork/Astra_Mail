#Requires -Version 5.1
[CmdletBinding()]
param([ValidatePattern('^[A-Za-z0-9][A-Za-z0-9_.@-]*$')][string]$SshHost = 'astra16',
      [string]$CloudRoot, [switch]$SkipCloudPublish, [switch]$SkipVersionBump)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'AstraMailRelease.ps1')

# Preflight is deliberately before the version mutation.
foreach ($name in @('ssh','scp','git')) { $null = Get-Command $name -ErrorAction Stop }
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
foreach ($name in @('Enter-WorkspaceReleaseLock.ps1','Get-CmakeAppVersionNames.ps1',
                    'Publish-ReleasePackageIfConfigured.ps1','Publish-PackageZipToCloud.ps1')) {
    $null = Resolve-AstraMailReleaseHelper $project $name
}
foreach ($file in Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1' -Recurse -File) {
    $tokens = $null; $errors = $null
    $null = [Management.Automation.Language.Parser]::ParseFile($file.FullName,[ref]$tokens,[ref]$errors)
    if ($errors.Count) { throw "PowerShell parse failed: $($file.FullName): $errors" }
}
if (!$SkipCloudPublish) {
    if (!$CloudRoot) { $CloudRoot = $env:WORKSPACE_RELEASE_CLOUD_ROOT }
    if (!$CloudRoot) { $CloudRoot = [Environment]::GetEnvironmentVariable('WORKSPACE_RELEASE_CLOUD_ROOT','User') }
    if ($CloudRoot -and !(Test-Path -LiteralPath $CloudRoot -PathType Container)) { throw 'Configured cloud root is unavailable' }
}
Invoke-AstraMailNative -File ssh -Arguments @('-o','BatchMode=yes',$SshHost,
    'test "$(uname -m)" = x86_64 && command -v cmake && command -v g++ && command -v python3 && command -v readelf && command -v tar && command -v sha256sum && test -f /usr/share/doc/libsodium18/copyright && test -f /usr/share/doc/libstdc++6/copyright && sudo -n unshare -m -n true')
$charset = Invoke-AstraMailNative -File ssh -Arguments @('-o','BatchMode=yes',$SshHost,'LC_ALL=C.UTF-8 locale charmap') -Capture
if ($charset -ne 'UTF-8') { throw 'Builder must support C.UTF-8 for Unicode package paths' }
$lockScript = Resolve-AstraMailReleaseHelper $project 'Enter-WorkspaceReleaseLock.ps1'
$lock = & $lockScript -ProjectRoot $project
try {
    $inputs = Get-AstraMailInputs $project
    $version = & (Join-Path $PSScriptRoot 'Sync-AstraMailVersion.ps1') -ProjectRoot $project -InputState $inputs -SkipVersionBump:$SkipVersionBump
    $id = (Get-Date -Format 'yyyyMMdd-HHmmss')+'-'+[Guid]::NewGuid().ToString('N').Substring(0,8)
    $local = Join-Path $project ('out/release-runs/'+$id)
    $null = New-Item -ItemType Directory -Path $local
    $head = Invoke-AstraMailNative -File git -Arguments @('-C',$project,'-c',"safe.directory=$($project.Replace('\','/'))",'rev-parse','HEAD') -Capture
    $status = Invoke-AstraMailNative -File git -Arguments @('-C',$project,'-c',"safe.directory=$($project.Replace('\','/'))",'status','--porcelain') -Capture
    $request = [pscustomobject]@{ Version = $version.Version; Head = $head; Dirty = [bool]$status; InputSha256 = $inputs.InputSha256 }
    Write-AstraMailJson (Join-Path $local 'input-state.json') $inputs
    Write-AstraMailJson (Join-Path $local 'build-request.json') $request
    $names = @($inputs.Files | ForEach-Object { $_.Path }) + @('app-version.json')
    $list = Join-Path $local 'source-files.txt'
    [IO.File]::WriteAllText($list, ($names -join "`n")+"`n", (New-Object Text.UTF8Encoding($false)))
    $archive = Join-Path $local 'source.zip'
    New-AstraMailSourceZip $archive $project $names
    $archiveSha = Get-AstraMailSha256 $archive
    $remote = Invoke-AstraMailNative -File ssh -Arguments @('-o','BatchMode=yes',$SshHost,'umask 077; mktemp -d /tmp/astramail-release.XXXXXXXX') -Capture
    if ($remote -notmatch '^/tmp/astramail-release\.[A-Za-z0-9]+$') { throw 'Unexpected remote staging path' }
    Write-Host "AstraMail $($version.Version): Release on $SshHost ($remote)"
    Write-AstraMailJson (Join-Path $local 'run.json') ([pscustomobject]@{ SshHost=$SshHost; RemoteDirectory=$remote; Version=$version.Version })
    Invoke-AstraMailNative -File scp -Arguments @($archive,(Join-Path $local 'input-state.json'),(Join-Path $local 'build-request.json'),"${SshHost}:$remote/")
    $command = "cd '$remote' && echo '$archiveSha  source.zip' | sha256sum -c - && mkdir source && python3 -m zipfile -e source.zip source && sh source/scripts/Build-AstraMailRelease.sh '$remote'"
    Invoke-AstraMailNative -File ssh -Arguments @('-o','BatchMode=yes',$SshHost,$command)
    Invoke-AstraMailNative -File scp -Arguments @("${SshHost}:$remote/artifacts.zip",(Join-Path $local 'artifacts.zip'))
    Expand-AstraMailArtifacts (Join-Path $local 'artifacts.zip') $local
    $package = & (Join-Path $PSScriptRoot 'New-AstraMailPackage.ps1') -PackageDirectory (Join-Path $local 'package') -ExpectedVersion $version.Version -ExpectedInputSha256 $inputs.InputSha256 -CloudRoot $CloudRoot -SkipCloudPublish:$SkipCloudPublish
    foreach ($path in @($package.PackageDirectory,$package.PackagedExe)) {
        if (!(Test-Path -LiteralPath $path)) { throw "Missing returned release artifact: $path" }
    }
    if (!$SkipCloudPublish -and $CloudRoot) {
        if (!$package.CloudPublished) { throw 'Configured publication did not complete' }
        foreach ($path in @($package.ZipPath,$package.Sha256Path)) {
            if (!$path -or !(Test-Path -LiteralPath $path -PathType Leaf)) { throw 'Missing returned published artifact' }
        }
        if ((Get-AstraMailSha256 $package.ZipPath) -cne $package.Sha256) { throw 'Published ZIP SHA256 mismatch' }
        # Validate the actual Windows-created cloud ZIP on the Linux consumer.
        $transport = Join-Path $local 'published-verify.zip'
        [IO.File]::Copy($package.ZipPath,$transport)
        Invoke-AstraMailNative -File scp -Arguments @($transport,"${SshHost}:$remote/published-verify.zip")
        $command = "cd '$remote' && LC_ALL=C.UTF-8 python3 -m zipfile -e published-verify.zip published-verify && sh published-verify/Verify-Package.sh && build/release_csv_driver published-verify/contacts-import-example.csv"
        Invoke-AstraMailNative -File ssh -Arguments @('-o','BatchMode=yes',$SshHost,$command)
        $package | Add-Member NoteProperty ArchiveVerifiedOnTarget $true
    }
    Write-AstraMailJson (Join-Path $local 'release-result.json') $package
    Write-Host "Release completed: $($package.PackageDirectory)"
    Write-Host "Result: $(Join-Path $local 'release-result.json')"
    $package
} finally {
    $lock.Mutex.ReleaseMutex()
    $lock.Mutex.Dispose()
}
