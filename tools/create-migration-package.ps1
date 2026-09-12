[CmdletBinding()]
param(
    [ValidateSet('UbuntuCore', 'FullRepository')]
    [string]$Profile = 'UbuntuCore',
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$projectName = Split-Path $projectRoot -Leaf
$workspaceRoot = Split-Path $projectRoot -Parent
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'

if (-not $OutputPath) {
    $outputDir = Join-Path $workspaceRoot 'transfer\ubuntu'
    New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
    $prefix = if ($Profile -eq 'UbuntuCore') { 'HomeMind-ubuntu-core' } else { 'HomeMind-repository' }
    $OutputPath = Join-Path $outputDir "$prefix-$stamp.tar.gz"
}

$OutputPath = [System.IO.Path]::GetFullPath($OutputPath)
$outputParent = Split-Path $OutputPath -Parent
New-Item -ItemType Directory -Path $outputParent -Force | Out-Null

$excludeArgs = @(
    "--exclude=$projectName/.venv",
    "--exclude=$projectName/.venv-*",
    "--exclude=$projectName/.venv-tools",
    "--exclude=$projectName/.git",
    "--exclude=__pycache__",
    "--exclude=.pytest_cache",
    "--exclude=.mypy_cache",
    "--exclude=*.pyc",
    "--exclude=$projectName/build",
    "--exclude=$projectName/out",
    "--exclude=$projectName/cmake_out",
    "--exclude=$projectName/server/data",
    "--exclude=$projectName/server/logs",
    "--exclude=$projectName/server/mqtt/data",
    "--exclude=$projectName/server/mqtt/log"
)

if ($Profile -eq 'UbuntuCore') {
    # These areas stay on Win11 or are frozen code/template branches.
    $excludeArgs += @(
        "--exclude=$projectName/miniprogram",
        "--exclude=$projectName/server",
        "--exclude=$projectName/quickapp",
        "--exclude=$projectName/app/hello_app",
        "--exclude=$projectName/board/contest_board",
        "--exclude=$projectName/logs/your-github-login"
    )
}

Push-Location $workspaceRoot
try {
    & tar.exe -czf $OutputPath @excludeArgs $projectName
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

$manifestPath = "$OutputPath.manifest.txt"
$artifactLines = @()
foreach ($name in @('nuttx.bin', 'nuttx.elf')) {
    $artifactPath = Join-Path $projectRoot "artifacts\$name"
    if (Test-Path -LiteralPath $artifactPath) {
        $artifactHash = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash.ToLowerInvariant()
        $artifactLines += "$name sha256=$artifactHash"
    }
}

$manifest = @(
    'HomeMind transfer manifest'
    "created=$((Get-Date).ToString('yyyy-MM-ddTHH:mm:ssK'))"
    "profile=$Profile"
    "source=$projectRoot"
    "archive=$(Split-Path $OutputPath -Leaf)"
    "archive_sha256=$hash"
    'windows_only=miniprogram/, ../外壳/'
    'secrets_included=not_checked'
    'operator_review_required=yes'
) + $artifactLines
Set-Content -LiteralPath $manifestPath -Encoding UTF8 -Value $manifest

Write-Host "Profile:           $Profile"
Write-Host "Migration package: $OutputPath"
Write-Host "SHA256 file:       $hashPath"
Write-Host "Manifest:          $manifestPath"
Write-Host "Archive size:      $((Get-Item $OutputPath).Length) bytes"
