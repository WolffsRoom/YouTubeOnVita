param([string]$Vita3K="C:\Users\wolff\Documents\Vita3k\Vita3K.exe")
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Vpk=Join-Path $Root 'build-starboard15\YouTubeVitaAVPlayerTest.vpk'
$Title='YTVAP0001'
$Profile=Join-Path $env:APPDATA 'Vita3K\Vita3K'
$Installed=Join-Path $Profile "ux0\app\$Title\eboot.bin"
$Log=Join-Path $Profile 'ux0\data\youtubevita\avplayer.log'
$Cfg=Join-Path $Root 'experiments\cobalt\vita\vita3k-test.yml'
if(!(Test-Path $Vpk) -or !(Test-Path $Installed) -or !(Test-Path $Cfg)){
  throw 'Install the diagnostic AVPlayer VPK in Vita3K before running the test'
}
if(Get-Process Vita3K -ErrorAction SilentlyContinue){
  throw 'Vita3K session already active; refusing to interrupt unrelated games'
}
$Command="import pathlib,zipfile;v=pathlib.Path(r'$Vpk');p=pathlib.Path(r'$Installed');p.write_bytes(zipfile.ZipFile(v).read('eboot.bin'))"
& python -c $Command
if($LASTEXITCODE -ne 0){throw 'Unable to update AVPlayer eboot.bin'}
$Offset=if(Test-Path $Log){(Get-Item $Log).Length}else{0}
$Process=Start-Process -FilePath $Vita3K -WorkingDirectory (Split-Path $Vita3K) -ArgumentList @('-c',$Cfg,'-r',$Title) -PassThru
try {
  $Complete=$false
  for($i=0;$i -lt 60;$i++){
    Start-Sleep -Milliseconds 350
    if(!(Test-Path $Log)){continue}
    $Text=[IO.File]::ReadAllText($Log)
    $Delta=$Text.Substring([Math]::Min([int]$Offset,$Text.Length))
    $Start=$Delta.LastIndexOf('START PSVita native SceAvPlayer genuine H264 MP4 playback API test')
    if($Start -lt 0){continue}
    $Chunk=$Delta.Substring($Start)
    if($Chunk -match 'RESULT (PASS|FAIL) \(([0-9]+) passed, ([0-9]+) failed\)'){
      if($Matches[1] -ne 'PASS' -or [int]$Matches[2] -ne 6 -or [int]$Matches[3] -ne 0){
        throw "Unexpected AVPlayer result: $($Matches[0])"
      }
      foreach($Evidence in @(
        'FRAME_COUNT 0x0000003C (60)',
        'CHANGING_FRAME_HASHES 0x0000003B (59)',
        'PASS SceAvPlayer produced nonconstant actual image pixels',
        'GPU_ALLOCATIONS_AFTER 0x00000000 (0)'
      )){
        if(!$Chunk.Contains($Evidence)){throw "Missing AVPlayer evidence: $Evidence"}
      }
      Write-Output 'VITA3K_AVPLAYER=PASS FRAMES=60 CHANGED=59 CHECKS=6 FAILURES=0'
      $Complete=$true
      break
    }
  }
  if(!$Complete){throw 'No fresh AVPlayer decode result within the test window'}
} finally {
  if(!$Process.HasExited){Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue}
}

