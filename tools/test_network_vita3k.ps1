param([string]$Vita3K="C:\Users\wolff\Documents\Vita3k\Vita3K.exe")
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Vpk=Join-Path $Root 'build-starboard15\StarboardVitaNetworkTest.vpk'
$App='YTVNT0001'
$Profile=Join-Path $env:APPDATA 'Vita3K\Vita3K'
$Log=Join-Path $Profile 'ux0\data\youtubevita\network.log'
$Cfg=Join-Path $Root 'experiments\cobalt\vita\vita3k-test.yml'
if(!(Test-Path $Vita3K) -or !(Test-Path $Vpk) -or !(Test-Path $Cfg)){
  throw 'Network test dependencies missing'
}
if(Get-Process Vita3K -ErrorAction SilentlyContinue){
  throw 'Vita3K is already running; this runner will not interrupt it'
}
$Self=Join-Path $Profile "ux0\app\$App\eboot.bin"
if(!(Test-Path $Self)){throw "Install $Vpk first through Vita3K"}
$Py="import zipfile,pathlib;v=zipfile.ZipFile(r'$Vpk');p=pathlib.Path(r'$Self');p.write_bytes(v.read('eboot.bin'))"
& python -c $Py
if($LASTEXITCODE -ne 0){throw 'Unable to update network test executable'}
$previous=if(Test-Path $Log){(Get-Item $Log).Length}else{0}
$process=Start-Process -FilePath $Vita3K -WorkingDirectory (Split-Path $Vita3K) -ArgumentList @('-c',$Cfg,'-r',$App) -PassThru
try {
  $complete=$false
  for($i=0;$i -lt 60;$i++){
    Start-Sleep -Milliseconds 350
    if(!(Test-Path $Log)){continue}
    $all=[IO.File]::ReadAllText($Log)
    $delta=$all.Substring([Math]::Min([int]$previous,$all.Length))
    $index=$delta.LastIndexOf('START Vita SceNet offline')
    if($index -lt 0){continue}
    $tail=$delta.Substring($index)
    if($tail -match 'RESULT (PASS|FAIL) \(([0-9]+) checks, ([0-9]+) failed\)'){
      $state=$Matches[1];$count=[int]$Matches[2];$failures=[int]$Matches[3]
      if($state -ne 'PASS' -or $count -ne 6 -or $failures -ne 0){
        throw "Network validation failed: state=$state checks=$count failed=$failures"
      }
      foreach($item in @(
        'PASS SceNet initialized on Vita3K',
        'PASS real Vita IPv4 UDP socket created',
        'PASS real Vita UDP socket closed',
        'PASS SceNet gracefully terminated'
      )){
        if(!$tail.Contains($item)){throw "Evidence missing: $item"}
      }
      Write-Output "VITA3K_NETWORK_TEST=PASS CHECKS=$count ERRORS=$failures"
      $complete=$true;break
    }
  }
  if(!$complete){throw 'No fresh SceNet test result'}
} finally {
  if(!$process.HasExited){Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue}
}

