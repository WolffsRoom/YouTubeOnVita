param([ValidateSet("Release","Debug")][string]$Configuration="Release")
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(!(Test-Path "$Root\experiments\cobalt\upstream-25\starboard\configuration.h")) {
  throw "Cobalt 25 sparse checkout missing; clone and pin upstream first"
}
& docker run --rm -v "${Root}:/src" -w /src -e VITASDK=/usr/local/vitasdk vitasdk/vitasdk:latest sh -lc "cmake -S . -B build-starboard15 -DCMAKE_BUILD_TYPE=$Configuration && cmake --build build-starboard15 -j4"
if($LASTEXITCODE -ne 0){throw "VitaSDK build failed"}
$Vpk=Join-Path $Root 'build-starboard15\StarboardVitaTest.vpk'
if(!(Test-Path $Vpk)){throw "Expected VPK missing: $Vpk"}
Write-Output "VPK generated: $Vpk"

