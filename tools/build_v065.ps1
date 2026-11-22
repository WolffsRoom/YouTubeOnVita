param(
  [ValidateSet('Normal','Lite','Both')][string]$Variant='Both',
  [switch]$Jitless
)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Folder=Join-Path $Root 'output\v065'
New-Item -Force -ItemType Directory -Path $Folder | Out-Null
$Safe=if ($Jitless) {'ON'} else {'OFF'}
$Suffix=if ($Jitless) {'JITless'} else {'JIT-on'}
$Candidates=@()
if ($Variant -ne 'Lite') { $Candidates+=@{File='YouTube.vpk';Lite='OFF';Dir='build-youtube-v065-full-'+$Suffix;Label='YouTube'} }
if ($Variant -ne 'Normal') { $Candidates+=@{File='YouTube-Lite.vpk';Lite='ON';Dir='build-youtube-v065-lite-'+$Suffix;Label='YouTube-Lite'} }
foreach ($Target in $Candidates) {
  $B=$Target.Dir
  $File=$Target.File
  $Tgt=$File+'-vpk'
  $Cmd='vdpm mbedtls && cmake -S . -B '+$B+
    ' -DCMAKE_BUILD_TYPE=Release -DYTV_NATIVE_HARDENED_CURL=ON'+
    ' -DYTV_PACKAGE_LITE='+$Target.Lite+
    ' -DYTV_NATIVE_DOWNLOAD_TEST=OFF -DYTV_COBALT_APP_VERSION=00.65 -DYTV_V061_RX_CHUNKS=ON -DYTV_V061_THREE_CORES=OFF -DYTV_V8_JITLESS_DIAGNOSTIC='+$Safe+
    ' && cmake --build '+$B+' --target '+$Tgt+' -j4'
  & docker run --rm -v ($Root+':/src') -w /src -e VITASDK=/usr/local/vitasdk vitasdk/vitasdk:latest sh -lc $Cmd
  if ($LASTEXITCODE -ne 0) { throw 'VPK build failed: '+$File }
  $From=Join-Path (Join-Path $Root $B) $File
  if (!(Test-Path $From)) { throw 'Missing VPK: '+$From }
  $To=Join-Path $Folder ($Target.Label+'-v0.65-'+$Suffix+'.vpk')
  Copy-Item $From $To -Force
  Write-Output ('TEST_VPK '+$To)
}
