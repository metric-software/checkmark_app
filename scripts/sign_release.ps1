<#
.SYNOPSIS
  Signs all release binaries (EXE, DLL) in the build output directory.
  Run this BEFORE building the installer to ensure all binaries are signed.

.DESCRIPTION
  This script signs all executable files in build/Release using the certificate
  configured in signing.local.ps1. Run this before creating the installer.

.EXAMPLE
  .\sign_release.ps1
  .\sign_release.ps1 -BuildDir "build\Release"
#>

[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [string]$BuildDir = "build\Release"
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\signing_common.ps1"

# Get signing configuration
$config = Get-SigningConfig
$null = Get-CertByThumbprint -Thumbprint $config.Thumbprint

# Resolve build directory
$buildPath = Resolve-Path -LiteralPath $BuildDir -ErrorAction SilentlyContinue
if (-not $buildPath) { throw "Build directory not found: $BuildDir (build Release first)" }

# Find all signable files
$excludeDirs = @("benchmark_results", "diagnostic_results", "debug logging", "profiles",
                  "installer", "comprison_data_files", "benchmark_user_data", "component_data", "showcase_files")
$extensions = @(".exe", ".dll", ".sys", ".ocx")

$files = Get-ChildItem -LiteralPath $buildPath.Path -Recurse -File | Where-Object {
  $extOk = $extensions -contains $_.Extension.ToLowerInvariant()
  if (-not $extOk) { return $false }
  $rel = $_.FullName.Substring($buildPath.Path.Length + 1)
  $top = ($rel -split "[\\/]")[0]
  return ($excludeDirs -notcontains $top)
}

if (-not $files) { throw "No signable files found under: $($buildPath.Path)" }

Write-Host "`n=== Signing Release Binaries ===" -ForegroundColor Green
Write-Host "Found $($files.Count) files to sign`n"

$signed = 0
$skipped = 0

foreach ($file in $files) {
  $result = Invoke-SignFile -SignTool $config.SignToolPath -FilePath $file.FullName `
    -Thumbprint $config.Thumbprint -TimestampUrl $config.TimestampUrl -SkipIfSigned
  if ($result -eq "Signed") { $signed++ } else { $skipped++ }
}

Write-Host "`n=== Summary ===" -ForegroundColor Green
Write-Host "Signed: $signed, Skipped (already signed): $skipped"

