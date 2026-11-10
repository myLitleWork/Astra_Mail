#Requires -Version 5.1
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$scripts = Join-Path $project 'scripts'
. (Join-Path $scripts 'AstraMailRelease.ps1')
$checks = 0
function Assert-Release($Value, [string]$Message) {
    if (!$Value) { throw $Message }
    $script:checks++
    Write-Host "PASS $Message"
}
foreach ($file in Get-ChildItem -LiteralPath $scripts -Recurse -Filter '*.ps1' -File) {
    $tokens=$null; $errors=$null
    $null=[Management.Automation.Language.Parser]::ParseFile($file.FullName,[ref]$tokens,[ref]$errors)
    Assert-Release ($errors.Count -eq 0) "PS 5.1 parser: $($file.Name)"
}
$testRoot = Join-Path $project ('out/release-tests/'+[Guid]::NewGuid().ToString('N'))
$fixture = Join-Path $testRoot 'project'
$null=New-Item -ItemType Directory -Path $fixture -Force
foreach ($root in @('src','secrets','cmake','scripts','tools','deployment','tutorials','tests')) { $null=New-Item -ItemType Directory -Path (Join-Path $fixture $root) }
foreach ($name in @('CMakeLists.txt','main.cpp','README.md','.gitignore','src/example.cpp')) { [IO.File]::WriteAllText((Join-Path $fixture $name),'synthetic') }
Write-AstraMailJson (Join-Path $fixture 'app-version.json') ([pscustomobject]@{formatVersion=1;major=3;minor=3;patch=0;productName='AstraMail'})
$state=Get-AstraMailInputs $fixture
$unicodeName = 'tutorials/'+[char]0x0418+[char]0x043C+[char]0x043F+'.md'
[IO.File]::WriteAllText((Join-Path $fixture $unicodeName),'synthetic unicode filename')
$zip = Join-Path $testRoot 'unicode.zip'
New-AstraMailSourceZip $zip $fixture @($unicodeName)
$archive = [IO.Compression.ZipFile]::OpenRead($zip)
try { Assert-Release ($archive.Entries[0].FullName -ceq $unicodeName) 'source ZIP preserves Unicode file names' }
finally { $archive.Dispose() }
$state=Get-AstraMailInputs $fixture
$first=& (Join-Path $scripts 'Sync-AstraMailVersion.ps1') -ProjectRoot $fixture -InputState $state
Assert-Release ($first.Version -eq '3.3.0') 'first sync preserves bootstrap version'
$again=& (Join-Path $scripts 'Sync-AstraMailVersion.ps1') -ProjectRoot $fixture
Assert-Release ($again.Version -eq '3.3.0') 'unchanged inputs do not consume patch'
$null=New-Item -ItemType Directory -Path (Join-Path $fixture '.astramail')
[IO.File]::WriteAllText((Join-Path $fixture '.astramail/synthetic-secret.bin'),'synthetic canary')
$withoutRuntime=Get-AstraMailInputs $fixture
Assert-Release ($withoutRuntime.InputSha256 -eq $state.InputSha256) 'runtime directory does not enter fingerprint'
[IO.File]::WriteAllText((Join-Path $fixture 'deployment/sample.csv'),'changed synthetic CSV')
$changed=& (Join-Path $scripts 'Sync-AstraMailVersion.ps1') -ProjectRoot $fixture
Assert-Release ($changed.Version -eq '3.3.1') 'CSV/template change bumps before compilation'
$same=& (Join-Path $scripts 'Sync-AstraMailVersion.ps1') -ProjectRoot $fixture
Assert-Release ($same.Version -eq '3.3.1') 'same release inputs do not bump twice'
[IO.File]::WriteAllText((Join-Path $fixture 'src/example.cpp'),'synthetic retry correction')
$retry=& (Join-Path $scripts 'Sync-AstraMailVersion.ps1') -ProjectRoot $fixture -SkipVersionBump
Assert-Release ($retry.Version -eq '3.3.1') 'failed-release retry can retain the synchronized version'
$failed=$false
try { Invoke-AstraMailNative -File "$env:SystemRoot/System32/WindowsPowerShell/v1.0/powershell.exe" -Arguments @('-NoProfile','-Command','exit 7') -Capture }
catch { $failed=$true }
Assert-Release $failed 'nonzero child exit is propagated'
Write-Host "RELEASE_SCRIPT_CHECKS=$checks/$checks"
