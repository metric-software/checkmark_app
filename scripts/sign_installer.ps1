<#
.SYNOPSIS
  Signs the final installer executable AFTER it has been built by Inno Setup.
  Run this AFTER the installer has been created.

.DESCRIPTION
  This script signs the installer EXE using the certificate configured in signing.local.ps1.
  By default, it auto-detects the installer path from version/app_version.iss.

.EXAMPLE
  .\sign_installer.ps1
  .\sign_installer.ps1 -InstallerPath "misc\installer\checkmark-1.0.0.0-x64.exe"
#>

[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [string]$InstallerPath
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\signing_common.ps1"

# Get signing configuration
$config = Get-SigningConfig
$null = Get-CertByThumbprint -Thumbprint $config.Thumbprint

# Resolve installer path
if (-not $InstallerPath) {
  $version = Read-VersionFromIss
  if ($version) {
    $InstallerPath = Join-Path $PSScriptRoot "..\misc\installer\checkmark-$version-x64.exe"
  } else {
    # Fallback: find most recent installer
    $pattern = Join-Path $PSScriptRoot "..\misc\installer\checkmark-*-x64.exe"
    $found = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($found) { $InstallerPath = $found.FullName }
  }
}

if (-not $InstallerPath -or -not (Test-Path -LiteralPath $InstallerPath)) {
  throw "Installer not found. Build the installer first, or pass -InstallerPath."
}

$InstallerPath = (Resolve-Path -LiteralPath $InstallerPath).Path

Write-Host "`n=== Signing Installer ===" -ForegroundColor Green
Write-Host "Installer: $InstallerPath`n"

Invoke-SignFile -SignTool $config.SignToolPath -FilePath $InstallerPath `
  -Thumbprint $config.Thumbprint -TimestampUrl $config.TimestampUrl

Write-Host "`nInstaller signed successfully!" -ForegroundColor Green

