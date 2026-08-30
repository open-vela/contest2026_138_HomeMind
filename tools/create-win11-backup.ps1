[CmdletBinding()]
param(
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$projectName = Split-Path $projectRoot -Leaf
$workspaceRoot = Split-Path $projectRoot -Parent
# Build the Chinese directory name from Unicode code points so this script
# also works in Windows PowerShell 5, which may decode UTF-8 files as ANSI.
$enclosureName = ([char]0x5916).ToString() + ([char]0x58F3).ToString()
$enclosurePath = Join-Path $workspaceRoot $enclosureName
$miniprogramPath = Join-Path $projectRoot 'miniprogram'

if (-not (Test-Path -LiteralPath $enclosurePath)) {
    throw "Enclosure directory not found: $enclosurePath"
}
if (-not (Test-Path -LiteralPath $miniprogramPath)) {
    throw "Miniprogram directory not found: $miniprogramPath"
}

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
if (-not $OutputPath) {
    $outputDir = Join-Path $workspaceRoot 'transfer\win11'
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
    $OutputPath = Join-Path $outputDir "HomeMind-win11-design-$stamp.tar.gz"
}
$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
New-Item -ItemType Directory -Path (Split-Path $OutputPath -Parent) -Force | Out-Null

Push-Location $workspaceRoot
try {
    & tar.exe -czf $OutputPath $enclosureName "$projectName/miniprogram"
    if ($LASTEXITCODE -ne 0) {
        throw "tar failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

$hash = (Get-FileHash -LiteralPath $OutputPath -Algorithm SHA256).Hash.ToLowerInvariant()
$hashPath = "$OutputPath.sha256"
Set-Content -LiteralPath $hashPath -Encoding ascii -NoNewline `
    -Value "$hash  $(Split-Path $OutputPath -Leaf)`n"

Write-Host "Win11 backup: $OutputPath"
Write-Host "SHA256 file:  $hashPath"
Write-Host "Archive size: $((Get-Item $OutputPath).Length) bytes"
