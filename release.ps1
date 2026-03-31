# JustSkip — Release Build Script
# Builds the project and packages release archives.
#
# Usage:
#   .\release.ps1              # Build + package
#   .\release.ps1 -SkipBuild   # Package only (uses existing build output)

param(
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$root     = $PSScriptRoot
$buildDir = Join-Path $root "build"
$binDir   = Join-Path $buildDir "bin\Release"
$relDir   = Join-Path $root "release"

# Read version from modinfo.json
$modinfo  = Get-Content (Join-Path $root "modinfo.json") | ConvertFrom-Json
$version  = "v$($modinfo.version)"
$verDir   = Join-Path $relDir $version

Write-Host "=== JustSkip $version release ===" -ForegroundColor Cyan

# ── Build ────────────────────────────────────────────────────

if (-not $SkipBuild) {
    Write-Host "Building..." -ForegroundColor Yellow

    if (-not (Test-Path $buildDir)) {
        cmake -B $buildDir -A x64
    }
    cmake --build $buildDir --config Release

    if ($LASTEXITCODE -ne 0) {
        Write-Host "ERROR: Build failed." -ForegroundColor Red
        exit 1
    }
    Write-Host "Build succeeded." -ForegroundColor Green
}

# Verify build output exists
$asiPath = Join-Path $binDir "JustSkip.asi"
$iniPath = Join-Path $root "JustSkip.ini"

if (-not (Test-Path $asiPath)) {
    Write-Host "ERROR: $asiPath not found. Run without -SkipBuild." -ForegroundColor Red
    exit 1
}

# ── Stage ────────────────────────────────────────────────────

$stageDir = Join-Path $root "build\stage"
if (Test-Path $stageDir) { Remove-Item $stageDir -Recurse -Force }
New-Item $stageDir -ItemType Directory | Out-Null

# Create version output directory
if (-not (Test-Path $verDir)) {
    New-Item $verDir -ItemType Directory | Out-Null
}

# ── Manual archive ───────────────────────────────────────────
# Flat: JustSkip.asi + JustSkip.ini
# Extracts directly into bin64/

Write-Host "Packaging Manual archive..." -ForegroundColor Yellow

$manualDir = Join-Path $stageDir "manual"
New-Item $manualDir -ItemType Directory | Out-Null
Copy-Item $asiPath $manualDir
Copy-Item $iniPath $manualDir

$manualZip = Join-Path $verDir "JustSkip-$version-Manual.zip"
if (Test-Path $manualZip) { Remove-Item $manualZip }
Compress-Archive -Path (Join-Path $manualDir "*") -DestinationPath $manualZip
Write-Host "  -> $manualZip" -ForegroundColor Green

# ── CDUMM archive ───────────────────────────────────────────
# Flat: JustSkip.asi + JustSkip.ini + modinfo.json
# CDUMM auto-detects ASI plugins from ZIP contents

Write-Host "Packaging CDUMM archive..." -ForegroundColor Yellow

$cdummDir = Join-Path $stageDir "cdumm"
New-Item $cdummDir -ItemType Directory | Out-Null
Copy-Item $asiPath $cdummDir
Copy-Item $iniPath $cdummDir
Copy-Item (Join-Path $root "modinfo.json") $cdummDir

$cdummZip = Join-Path $verDir "JustSkip-$version-CDUMM.zip"
if (Test-Path $cdummZip) { Remove-Item $cdummZip }
Compress-Archive -Path (Join-Path $cdummDir "*") -DestinationPath $cdummZip
Write-Host "  -> $cdummZip" -ForegroundColor Green

# ── CDMM archive ─────────────────────────────────────────────
# Structure: _asi/JustSkip/JustSkip.asi + JustSkip.ini
# JSON Mod Manager expects _asi/<ModName>/ layout

Write-Host "Packaging CDMM archive..." -ForegroundColor Yellow

$cdmmDir = Join-Path $stageDir "cdmm\_asi\JustSkip"
New-Item $cdmmDir -ItemType Directory -Force | Out-Null
Copy-Item $asiPath $cdmmDir
Copy-Item $iniPath $cdmmDir

$cdmmZip = Join-Path $verDir "JustSkip-$version-CDMM.zip"
if (Test-Path $cdmmZip) { Remove-Item $cdmmZip }
Compress-Archive -Path (Join-Path $stageDir "cdmm\*") -DestinationPath $cdmmZip
Write-Host "  -> $cdmmZip" -ForegroundColor Green

# ── Cleanup ──────────────────────────────────────────────────

Remove-Item $stageDir -Recurse -Force

Write-Host ""
Write-Host "Release $version ready:" -ForegroundColor Cyan
Write-Host "  $manualZip"
Write-Host "  $cdummZip"
Write-Host "  $cdmmZip"
