param([string]$ProjectRoot)
$ErrorActionPreference='Stop'
if ([string]::IsNullOrWhiteSpace($ProjectRoot)) { $ProjectRoot=$env:PERSONALCEF_PROJECT_ROOT }
$ProjectRoot=[IO.Path]::GetFullPath($ProjectRoot)
foreach($p in @('build\vs2022','build\bootstrap','tests\cefclient','tests\shared','tests\.cef-version')) {
  $x=Join-Path $ProjectRoot $p
  if(Test-Path -LiteralPath $x){ Remove-Item -LiteralPath $x -Recurse -Force }
}
Write-Host 'Generated build/upstream files removed. bin/ and src/ were preserved.'
