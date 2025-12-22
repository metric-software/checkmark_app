[CmdletBinding(SupportsShouldProcess = $true)]
param(
  [string]$BuildDir = "build\\Release",
  [string]$Thumbprint,
  [string]$TimestampUrl,
  [string]$SignToolPath,
  [switch]$SignInstaller,
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

function Get-PayloadCandidates {
  param([Parameter(Mandatory = $true)][string]$RootDir)

  $root = (Resolve-Path -LiteralPath $RootDir).Path
  $excludeTopDirs = @(
    "benchmark_results",
    "diagnostic_results",
    "debug logging",
    "profiles",
    "installer",
    "comprison_data_files",
    "benchmark_user_data",
    "component_data",
    "showcase_files"
  )

  $extensions = @(".exe", ".dll", ".sys", ".ocx")

  Get-ChildItem -LiteralPath $root -Recurse -File |
    Where-Object {
      $extOk = $extensions -contains $_.Extension.ToLowerInvariant()
      if (-not $extOk) { return $false }
      $rel = $_.FullName.Substring($root.Length + 1)
      $top = ($rel -split "[\\\\/]")[0]
      return ($excludeTopDirs -notcontains $top)
    }
}

function Sign-File {
  param(
    [Parameter(Mandatory = $true)][string]$SignTool,
    [Parameter(Mandatory = $true)][string]$FilePath,
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

  $sig = Get-AuthenticodeSignature -FilePath $FilePath
  if ($sig.Status -eq "Valid") { return "SkippedAlreadySigned" }
  if ($sig.Status -ne "NotSigned") {
    throw "Unexpected signature status for ${FilePath}: $($sig.Status). Refusing to overwrite/append."
  }

  $args = @(
    "sign",
    "/fd", "sha256",
    "/td", "sha256",
    "/tr", $timestamp,
    "/sha1", $Sha1Thumbprint,
    "/v",
    $FilePath
  )

  if ($PSCmdlet.ShouldProcess($FilePath, "Sign")) {
    & $SignTool @args | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "signtool sign failed (exit $LASTEXITCODE): $FilePath" }
    return "Signed"
  }

  return "WouldSign"
}

function Verify-File {
  param([Parameter(Mandatory = $true)][string]$SignTool, [Parameter(Mandatory = $true)][string]$FilePath)
  $args = @("verify", "/pa", "/v", $FilePath)
  if ($PSCmdlet.ShouldProcess($FilePath, "Verify signature")) {
    & $SignTool @args | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "signtool verify failed (exit $LASTEXITCODE): $FilePath" }
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

# Validate cert exists (and ensure we fail early before signing loop).
$null = Get-CertByThumbprint -Sha1 $effectiveThumbprint

$buildPath = Resolve-Path -LiteralPath $BuildDir -ErrorAction SilentlyContinue
if (-not $buildPath) { throw "Build directory not found: $BuildDir (build Release first)" }

$items = Get-PayloadCandidates -RootDir $buildPath.Path
if (-not $items) { throw "No signable payload files found under: $($buildPath.Path)" }

$results = @()
foreach ($item in $items) {
  $action = Sign-File -SignTool $effectiveSignTool -FilePath $item.FullName -Sha1Thumbprint $effectiveThumbprint -Rfc3161Url $effectiveTimestamp
  $results += [pscustomobject]@{ Path = $item.FullName; Action = $action }
}

# Optional: sign the final installer output.
if ($SignInstaller) {
  $installer = $InstallerPath
  if (-not $installer) {
    $v = Read-VersionFromIss
    if (-not $v) { throw "InstallerPath not provided and version/app_version.iss could not be read to infer the installer filename." }
    $installer = Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..\\misc\\installer")).Path ("checkmark-$v-x64.exe")
  }
  if (-not (Test-Path -LiteralPath $installer)) { throw "Installer not found: $installer" }

  $action = Sign-File -SignTool $effectiveSignTool -FilePath $installer -Sha1Thumbprint $effectiveThumbprint -Rfc3161Url $effectiveTimestamp
  $results += [pscustomobject]@{ Path = $installer; Action = $action }
}

# Verify anything we signed this run.
foreach ($r in $results | Where-Object { $_.Action -eq "Signed" }) {
  Verify-File -SignTool $effectiveSignTool -FilePath $r.Path
}

$results | Sort-Object Action,Path | Format-Table -AutoSize
