$SigningConfig = @{
  # Cert selection (required): paste your cert thumbprint (no spaces)
  Thumbprint = "PASTE_THUMBPRINT_HERE"

  # Timestamping (recommended): RFC3161 timestamp server
  TimestampUrl = "http://timestamp.digicert.com"

  # Optional: explicitly set signtool.exe path (x64 preferred)
  SignToolPath = "C:\\Program Files (x86)\\Windows Kits\\10\\bin\\10.0.26100.0\\x64\\signtool.exe"
}

