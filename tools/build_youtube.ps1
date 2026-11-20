param(
  [ValidateSet("Release","Debug")][string]$Configuration = "Release",
  [ValidateSet("Normal","Lite","Both")][string]$Variant = "Both"
)
$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Current = Join-Path $Root 'current'
New-Item -ItemType Directory -Path $Current -Force | Out-Null

# Same native source, two independent title IDs:
# YouTube: YTVCB0001, ATTRIBUTE2=12 (extended RAM)
# YouTube Lite: YTVCB0002, ATTRIBUTE2=0 (regular RAM)
$Builds = @()
if ($Variant -ne "Lite") {
  $Builds += @{ Name="YouTube"; Dir="build-youtube"; File="YouTube.vpk"; Lite="OFF" }
}
if ($Variant -ne "Normal") {
  $Builds += @{ Name="YouTube Lite"; Dir="build-youtube-lite"; File="YouTube-Lite.vpk"; Lite="ON" }
}
foreach ($b in $Builds) {
  $Filename = $b.File
  $Dir = $b.Dir
  $Toggle = $b.Lite
  $Command = 'vdpm mbedtls && cmake -S . -B ' + $Dir +
    ' -DCMAKE_BUILD_TYPE=' + $Configuration +
    ' -DYTV_NATIVE_HARDENED_CURL=ON -DYTV_PACKAGE_LITE=' + $Toggle +
    ' && cmake --build ' + $Dir + ' --target ' + $Filename + '-vpk -j4'
  & docker run --rm -v ($Root + ':/src') -w /src -e VITASDK=/usr/local/vitasdk vitasdk/vitasdk:latest sh -lc $Command
  if ($LASTEXITCODE -ne 0) { throw "Build failed: $($b.Name)" }
  $Vpk = Join-Path (Join-Path $Root $Dir) $Filename
  if (!(Test-Path $Vpk)) { throw "Missing VPK: $Vpk" }
  Copy-Item -Force $Vpk (Join-Path $Current $Filename)
  Write-Output "$($b.Name): $Current\$Filename"
}
