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
$htmlName='tutorials/guide.html'
$docName='tutorials/manual.doc'
$cssName='tutorials/style.css'
[IO.File]::WriteAllText((Join-Path $fixture $cssName),'body { color: black; }')
[IO.File]::WriteAllText((Join-Path $fixture 'src/not-a-release-style.css'),'synthetic excluded css')
$installerName='scripts/install_astramail.sh'
[IO.File]::WriteAllText((Join-Path $fixture $htmlName),'<html>synthetic guide</html>')
$binaryDoc=[byte[]]@(0xD0,0xCF,0x11,0xE0,0xA1,0xB1,0x1A,0xE1,0,255,128)
[IO.File]::WriteAllBytes((Join-Path $fixture $docName),$binaryDoc)
[IO.File]::WriteAllText((Join-Path $fixture $installerName),'# synthetic installer; never executed')
[IO.File]::WriteAllBytes((Join-Path $fixture 'src/not-a-release-document.doc'),$binaryDoc)
$withDocuments=Get-AstraMailInputs $fixture
foreach($name in @($htmlName,$docName,$cssName,$installerName)) {
    Assert-Release ($name -in @($withDocuments.Files.Path)) "release input includes $name"
}
Assert-Release ('src/not-a-release-style.css' -notin @($withDocuments.Files.Path)) 'CSS allowlist is limited to tutorials'
Assert-Release ('src/not-a-release-document.doc' -notin @($withDocuments.Files.Path)) 'DOC allowlist is limited to tutorials'
Assert-Release ($withDocuments.InputSha256 -ne $state.InputSha256) 'added operator documents change release fingerprint'
$documentZip=Join-Path $testRoot 'documents.zip'
New-AstraMailSourceZip $documentZip $fixture @($htmlName,$docName,$installerName)
$archive=[IO.Compression.ZipFile]::OpenRead($documentZip)
try {
    $entry=$archive.GetEntry($docName)
    $buffer=New-Object IO.MemoryStream
    $stream=$entry.Open()
    try { $stream.CopyTo($buffer); $copy=$buffer.ToArray() }
    finally { $stream.Dispose(); $buffer.Dispose() }
    Assert-Release ([Convert]::ToBase64String($copy) -ceq [Convert]::ToBase64String($binaryDoc)) 'binary DOC survives source ZIP byte-for-byte'
} finally { $archive.Dispose() }
$beforeEdit=$withDocuments.InputSha256
[IO.File]::WriteAllText((Join-Path $fixture $htmlName),'<html>updated synthetic guide</html>')
Assert-Release ((Get-AstraMailInputs $fixture).InputSha256 -ne $beforeEdit) 'HTML edit changes release fingerprint'
$beforeEdit=(Get-AstraMailInputs $fixture).InputSha256
[IO.File]::AppendAllText((Join-Path $fixture $docName),'synthetic update')
Assert-Release ((Get-AstraMailInputs $fixture).InputSha256 -ne $beforeEdit) 'DOC edit changes release fingerprint'
$beforeEdit=(Get-AstraMailInputs $fixture).InputSha256
[IO.File]::AppendAllText((Join-Path $fixture $cssName),'p { color: blue; }')
Assert-Release ((Get-AstraMailInputs $fixture).InputSha256 -ne $beforeEdit) 'CSS edit changes release fingerprint'
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
