param([string]$Vita3K="C:\Users\wolff\Documents\Vita3k\Vita3K.exe",
      [int]$ExpectedChecks=33)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Vpk=Join-Path $Root 'build-starboard15\YouTubeVitaGraphicsTest.vpk'
$Cfg=Join-Path $Root 'experiments\cobalt\vita\vita3k-test.yml'
$Profile=Join-Path $env:APPDATA 'Vita3K\Vita3K'
$Log=Join-Path $Profile 'ux0\data\youtubevita\graphics.log'
$App='YTVGX0001'
$Self=Join-Path $Profile "ux0\app\$App\eboot.bin"
if(!(Test-Path $Vpk) -or !(Test-Path $Vita3K) -or !(Test-Path $Cfg)) {
  throw 'Missing graphical test dependencies'
}
if(Get-Process Vita3K -ErrorAction SilentlyContinue){
  throw 'Vita3K already running; this script does not close unrelated sessions'
}
if(!(Test-Path $Self)){throw "Install $Vpk in Vita3K once before using this runner"}
$Py="import pathlib,zipfile;v=zipfile.ZipFile(r'$Vpk');o=pathlib.Path(r'$Self');o.write_bytes(v.read('eboot.bin'))"
& python -c $Py
if($LASTEXITCODE -ne 0){throw 'Failed to update graphics test binary'}
$previous=if(Test-Path $Log){(Get-Item $Log).Length}else{0}
$proc=Start-Process -FilePath $Vita3K -WorkingDirectory (Split-Path $Vita3K) -ArgumentList @('-c',$Cfg,'-r',$App) -PassThru
try {
  $complete=$false
  for($i=0;$i -lt 80;$i++) {
    Start-Sleep -Milliseconds 350
    if(!(Test-Path $Log)){continue}
    $all=[IO.File]::ReadAllText($Log)
    $increment=$all.Substring([Math]::Min([int]$previous,$all.Length))
    $start=$increment.LastIndexOf('START Native PSVita vitaGL EGL-GXM framebuffer smoke test')
    if($start -lt 0){continue}
    $tail=$increment.Substring($start)
    if($tail -match 'RESULT (PASS|FAIL) \(([0-9]+) passed, ([0-9]+) failed\)'){
      $status=$Matches[1];$count=[int]$Matches[2];$errors=[int]$Matches[3]
      if($status -ne 'PASS' -or $count -ne $ExpectedChecks -or $errors -ne 0) {
        throw "Graphics failure: state=$status checks=$count errors=$errors"
      }
      if(!$tail.Contains('PRESENTED_FRAMES 40') -or
         !$tail.Contains('PASS real Cobalt Starboard EGL and GLES entry tables available') -or
         !$tail.Contains('PASS 40 real GXM frames presented via EGL SwapBuffers')) {
        throw 'Missing proof of EGL/GLES bridge and 40 presented frames'
      }
      Write-Output "VITA3K_GRAPHICS_TEST=PASS CHECKS=$count FRAMES=40 ERRORS=$errors"
      $complete=$true;break
    }
  }
  if(!$complete){throw 'No fresh graphics test result in Vita3K log'}
} finally {
  if(!$proc.HasExited){Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue}
}

