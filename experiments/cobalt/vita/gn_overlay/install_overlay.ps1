param([switch]$Register=$true)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$Cobalt=Join-Path $Root 'experiments\cobalt\upstream-25'
$Src=Join-Path $PSScriptRoot 'starboard\vita\armv7'
$Dest=Join-Path $Cobalt 'starboard\vita\armv7'
if (!(Test-Path (Join-Path $Cobalt '.git'))) { throw 'Cobalt checkout missing' }
New-Item -ItemType Directory -Force -Path $Dest | Out-Null
Copy-Item (Join-Path $Src '*') -Destination $Dest -Recurse -Force
$File=Join-Path $Cobalt 'starboard\build\platforms.py'
$Text=Get-Content -Raw $File
if ($Register -and $Text -notmatch "'vita-armv7':") {
  $Anchor="    'raspi-2': 'starboard/raspi/2',"
  if(-not $Text.Contains($Anchor)){throw 'Upstream platforms.py anchor changed'}
  $Text=$Text.Replace($Anchor,$Anchor+[Environment]::NewLine+"    'vita-armv7': 'starboard/vita/armv7',")
  Set-Content -Path $File -Value $Text -Encoding UTF8
}
Write-Output "GN overlay installed: $Dest"

