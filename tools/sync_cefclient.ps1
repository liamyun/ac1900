param(
  [string]$ProjectRoot,
  [string]$CefRoot,
  [switch]$ForceSync
)

$ErrorActionPreference = 'Stop'
$parts = @(
  (Join-Path $PSScriptRoot 'sync_cefclient.ps1.part1'),
  (Join-Path $PSScriptRoot 'sync_cefclient.ps1.part2'),
  (Join-Path $PSScriptRoot 'sync_cefclient.ps1.part3')
)
foreach ($part in $parts) {
  if (!(Test-Path -LiteralPath $part)) { throw "Missing sync source fragment: $part" }
}
$materialized = Join-Path $PSScriptRoot '.sync_cefclient.materialized.ps1'
$text = ($parts | ForEach-Object { [IO.File]::ReadAllText($_) }) -join ''
[IO.File]::WriteAllText($materialized, $text, (New-Object System.Text.UTF8Encoding($false)))
$argsList = @()
if (![string]::IsNullOrWhiteSpace($ProjectRoot)) { $argsList += @('-ProjectRoot', $ProjectRoot) }
if (![string]::IsNullOrWhiteSpace($CefRoot)) { $argsList += @('-CefRoot', $CefRoot) }
if ($ForceSync) { $argsList += '-ForceSync' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $materialized @argsList
exit $LASTEXITCODE
