[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [string]$Thumbprint,
  [string]$TimestampUrl,
  [string]$SignToolPath,
  [string]$InstallerPath
)

$ErrorActionPreference = "Stop"

function Resolve-SignToolPath {
  param([string]$ExplicitPath)

  if ($ExplicitPath) {
    if (-not (Test-Path -LiteralPath $ExplicitPath)) { throw "signtool.exe not found at: $ExplicitPath" }
    return (Resolve-Path -LiteralPath $ExplicitPath).Path
  }

  $candidates = @(
    "C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.*\\x64\\signtool.exe",
    "C:\\Program Files (x86)\\Windows Kits\\10\\bin\\*\\x64\\signtool.exe",
    "C:\\Program Files (x86)\\Windows Kits\\10\\App Certification Kit\\signtool.exe"
  )

  $all = @()
  foreach ($pattern in $candidates) {
    $all += Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue
  }
  if ($all) {
    $best =
      $all |
      Select-Object -Unique FullName |
      Sort-Object @{
        Expression = {
          if ($_.FullName -match "\\\\bin\\\\([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)\\\\x64\\\\signtool\\.exe$") {
            try { return [version]$Matches[1] } catch { return [version]"0.0.0.0" }
          }
          return [version]"0.0.0.0"
        }
      } -Descending |
      Select-Object -First 1
    return $best.FullName
  }

  throw "Could not find signtool.exe. Install Windows SDK or set SignToolPath in scripts/signing.local.ps1."
}

function Get-CertByThumbprint {
  param([Parameter(Mandatory = $true)][string]$Sha1)
  $normalized = ($Sha1 -replace "\\s+", "").ToUpperInvariant()
  if ($normalized -notmatch "^[0-9A-F]{40}$") { throw "Thumbprint must be 40 hex chars (SHA-1), got: '$Sha1'" }

  $stores = @("Cert:\\CurrentUser\\My", "Cert:\\LocalMachine\\My")
  foreach ($store in $stores) {
    $hit = Get-ChildItem -Path $store -ErrorAction SilentlyContinue | Where-Object { $_.Thumbprint -eq $normalized } | Select-Object -First 1
    if ($hit) { return $hit }
  }

  throw "Certificate with thumbprint $normalized not found in CurrentUser\\My or LocalMachine\\My."
}

function Read-VersionFromIss {
  $vf = Join-Path $PSScriptRoot "..\\version\\app_version.iss"
  if (-not (Test-Path -LiteralPath $vf)) { return $null }
  $raw = Get-Content -LiteralPath $vf -Raw
  if ($raw -match '"([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)"') { return $Matches[1] }
  return $null
}

function Resolve-DefaultInstallerPath {
  $v = Read-VersionFromIss
  if ($v) {
    $dir = Resolve-Path (Join-Path $PSScriptRoot "..\\misc\\installer")
    return (Join-Path $dir.Path ("checkmark-$v-x64.exe"))
  }

  $fallbackCandidates = @(
    (Join-Path $PSScriptRoot "..\\misc\\installer\\checkmark-*-x64.exe"),
    (Join-Path $PSScriptRoot "..\\build\\Release\\installer\\checkmark-*-x64.exe")
  )

  foreach ($pattern in $fallbackCandidates) {
    $hits = Get-ChildItem -Path $pattern -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending
    if ($hits) {
      return $hits[0].FullName
    }
  }

  throw "InstallerPath not provided and no installer exe found. Pass -InstallerPath 'path\\to\\checkmark-<version>-x64.exe'."
}

function Sign-Installer {
  param(
    [Parameter(Mandatory = $true)][string]$SignTool,
    [Parameter(Mandatory = $true)][string]$Installer,
    [Parameter(Mandatory = $true)][string]$Sha1Thumbprint,
    [Parameter(Mandatory = $true)][string]$Rfc3161Url
  )

  if ([string]::IsNullOrWhiteSpace($Rfc3161Url)) {
    throw "Timestamp URL is required; refusing to sign without RFC3161 timestamping."
  }
  $timestamp = $Rfc3161Url.Trim()
  if ($timestamp -notmatch "^https?://") {
    throw "TimestampUrl must start with http:// or https:// (got: '$Rfc3161Url')"
  }

  $sig = Get-AuthenticodeSignature -FilePath $Installer
  if ($sig.Status -eq "Valid") {
    Write-Host "Installer is already signed (Valid): $Installer"
    return
  }
  if ($sig.Status -ne "NotSigned") {
    throw "Unexpected signature status for ${Installer}: $($sig.Status). Refusing to overwrite/append."
  }

  $args = @(
    "sign",
    "/fd", "sha256",
    "/td", "sha256",
    "/tr", $timestamp,
    "/sha1", $Sha1Thumbprint,
    "/v",
    $Installer
  )

  if ($PSCmdlet.ShouldProcess($Installer, "Sign installer")) {
    & $SignTool @args | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "signtool sign failed (exit $LASTEXITCODE): $Installer" }
  }

  if ($PSCmdlet.ShouldProcess($Installer, "Verify installer signature")) {
    & $SignTool verify /pa /v $Installer | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "signtool verify failed (exit $LASTEXITCODE): $Installer" }
  }
}

# Load per-machine config (gitignored).
$localConfigPath = Join-Path $PSScriptRoot "signing.local.ps1"
if (Test-Path -LiteralPath $localConfigPath) {
  . $localConfigPath
}
if (-not $SigningConfig) {
  throw "Missing signing config. Create scripts/signing.local.ps1 based on scripts/signing.example.ps1."
}

$effectiveThumbprint = if ($Thumbprint) { $Thumbprint } else { $SigningConfig.Thumbprint }
$effectiveTimestamp  = if ($TimestampUrl) { $TimestampUrl } else { $SigningConfig.TimestampUrl }
$signToolInput       = if ($SignToolPath) { $SignToolPath } else { $SigningConfig.SignToolPath }
$effectiveSignTool   = Resolve-SignToolPath $signToolInput

if (-not $effectiveThumbprint) { throw "Thumbprint is required (set in scripts/signing.local.ps1 or pass -Thumbprint)." }
if (-not $effectiveTimestamp)  { throw "TimestampUrl is required (set in scripts/signing.local.ps1 or pass -TimestampUrl)." }

# Validate cert exists (fail early).
$null = Get-CertByThumbprint -Sha1 $effectiveThumbprint

$installer = if ($InstallerPath) { $InstallerPath } else { Resolve-DefaultInstallerPath }
if (-not (Test-Path -LiteralPath $installer)) { throw "Installer not found: $installer" }

Sign-Installer -SignTool $effectiveSignTool -Installer $installer -Sha1Thumbprint $effectiveThumbprint -Rfc3161Url $effectiveTimestamp
