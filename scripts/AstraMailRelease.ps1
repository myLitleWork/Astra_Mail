#Requires -Version 5.1
$ErrorActionPreference = 'Stop'

function Resolve-AstraMailReleaseHelper([string]$ProjectRoot, [string]$Name) {
    & (Join-Path $PSScriptRoot 'Find-WorkspaceReleaseRoot.ps1') -StartPath $ProjectRoot -Name $Name
}

function Get-AstraMailSha256([string]$Path) {
    $sha = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($Path)
    try { [BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','').ToLowerInvariant() }
    finally { $stream.Dispose(); $sha.Dispose() }
}

function New-AstraMailSourceZip([string]$Path, [string]$ProjectRoot, [string[]]$Names) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::Open($Path,[IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($rel in $Names) {
            $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,
                (Join-Path $ProjectRoot $rel),$rel,[IO.Compression.CompressionLevel]::Optimal)
        }
    } finally { $archive.Dispose() }
}

function Expand-AstraMailArtifacts([string]$Path, [string]$Destination) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [IO.Compression.ZipFile]::OpenRead($Path)
    try {
        foreach ($entry in $archive.Entries) {
            $rel = $entry.FullName
            if ($rel.StartsWith('/') -or $rel.Contains('\') -or $rel.Contains(':') -or $rel -match '(^|/)\.{1,2}(/|$)' -or
                ($rel -notlike 'package/*' -and $rel -notin @('native-tests.log','packaged-tests.log','packaged-tests.json','configure.log','build.log'))) {
                throw "Unsafe artifact archive path: $rel"
            }
        }
    } finally { $archive.Dispose() }
    [IO.Compression.ZipFile]::ExtractToDirectory($Path,$Destination)
}

function Get-AstraMailInputs([string]$ProjectRoot) {
    $base = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\','/')
    $paths = New-Object 'System.Collections.Generic.List[string]'
    foreach ($leaf in @('CMakeLists.txt','main.cpp','README.md','.gitignore')) {
        $paths.Add($leaf)
    }
    # Deliberately bounded roots; never enumerate the checkout/runtime/build root.
    foreach ($root in @('src','secrets','cmake','scripts','tools','deployment','tutorials','tests')) {
        $pending = New-Object 'System.Collections.Generic.Stack[string]'
        $pending.Push((Join-Path $base $root))
        while ($pending.Count) {
            $dir = $pending.Pop()
            $item = Get-Item -LiteralPath $dir -Force
            if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point in release inputs: $dir" }
            foreach ($file in Get-ChildItem -LiteralPath $dir -Force) {
                if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Reparse point in release inputs: $($file.FullName)" }
                if ($file.PSIsContainer) { $pending.Push($file.FullName); continue }
                if ($file.Name -eq 'CMakeLists.txt' -or $file.Extension -match '^\.(cpp|c|h|in|cmake|ps1|py|sh|bat|csv|md)$') {
                    $paths.Add($file.FullName.Substring($base.Length+1).Replace('\','/'))
                }
            }
        }
    }
    $records = @($paths | Sort-Object -Unique | ForEach-Object {
        $full = Join-Path $base $_
        [pscustomobject]@{ Path = $_; Sha256 = Get-AstraMailSha256 $full }
    })
    $text = ($records | ForEach-Object { $_.Path + '  ' + $_.Sha256 }) -join "`n"
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $hash = [BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($text+"`n"))).Replace('-','').ToLowerInvariant() }
    finally { $sha.Dispose() }
    [pscustomobject]@{ FormatVersion = 1; InputSha256 = $hash; Files = $records }
}

function Invoke-AstraMailNative {
    param([string]$File, [string[]]$Arguments, [switch]$Capture)
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        if ($Capture) { $output = @(& $File @Arguments 2>&1) }
        else { & $File @Arguments 2>&1 | ForEach-Object { Write-Host ([string]$_) } }
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $saved }
    if ($code -ne 0) {
        if ($Capture) { $output | ForEach-Object { Write-Host ([string]$_) } }
        throw "$File failed with exit code $code"
    }
    if ($Capture) { return (($output | ForEach-Object { [string]$_ }) -join "`n").Trim() }
}

function Write-AstraMailJson([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 12)+"`n", (New-Object Text.UTF8Encoding($false)))
}
