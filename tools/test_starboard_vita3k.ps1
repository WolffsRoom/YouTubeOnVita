param([string]$Vita3K="C:\Users\wolff\Documents\Vita3k\Vita3K.exe")
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Vpk=Join-Path $Root 'build-starboard15\StarboardVitaTest.vpk'
$App='YTVSB0001'
$VitaProfile=Join-Path $env:APPDATA 'Vita3K\Vita3K'
$Log=Join-Path $VitaProfile 'ux0\data\youtubevita\starboard15.log'
if(!(Test-Path $Vita3K)){throw "Vita3K executable missing"}
if(!(Test-Path $Vpk)){throw "Build the VPK first"}
if(Get-Process Vita3K -ErrorAction SilentlyContinue){
  throw "Vita3K is already open; close it before this automated test (no app will be terminated)"
}
# Import the already installed app SELF or install the VPK first.
$AppSelf=Join-Path $VitaProfile "ux0\app\$App\eboot.bin"
if(!(Test-Path $AppSelf)){throw "App not yet installed in Vita3K. Install $Vpk first"}
# Extract only own test app eboot.bin; no other installed title is modified.
$Py="import zipfile,pathlib;v=pathlib.Path(r'$Vpk');o=pathlib.Path(r'$AppSelf');o.write_bytes(zipfile.ZipFile(v).read('eboot.bin'))"
& python -c $Py
if($LASTEXITCODE -ne 0){throw "Failed to update test SELF"}
$Cfg=Join-Path $Root 'experiments\cobalt\vita\vita3k-test.yml'
if(!(Test-Path $Cfg)){throw "Local test profile missing"}
$PreviousSize=if(Test-Path $Log){(Get-Item $Log).Length}else{0}
$Process=Start-Process -FilePath $Vita3K -WorkingDirectory (Split-Path $Vita3K) -ArgumentList @('-c',$Cfg,'-r',$App) -PassThru
try {
  $Result=$null
  for($i=0;$i -lt 260;$i++){
    Start-Sleep -Milliseconds 350
    if(Test-Path $Log) {
      try { $Body=[IO.File]::ReadAllText($Log) } catch [System.IO.IOException] { Start-Sleep -Milliseconds 100; continue }
      if($Body.Length -gt 0) {
        $Appended=$Body.Substring([Math]::Min([int]$PreviousSize,$Body.Length)); $LastStart=$Appended.LastIndexOf('START Starboard 15')
        if($LastStart -ge 0) {
          $Tail=$Appended.Substring($LastStart)
          if($Tail -match 'RESULT (PASS|FAIL) \(([0-9]+) errors\)') {
            $Result=$Matches[1]
            $Errors=[int]$Matches[2]
            $Passed=([regex]::Matches($Tail,'(?m)^PASS ')).Count
            if($Passed -ne 192){throw "EXPECTED_STARBOARD_CHECK_COUNT 192, got $Passed"}
            Write-Output "VITA3K_TEST=$Result CHECKS_PASSED=$Passed ERRORS=$Errors"
            break
          }
        }
      }
    }
  }
  if(!$Result){throw "No test result in emulator log"}
  if($Result -ne 'PASS' -or $Errors -ne 0){throw "Starboard API checks failed"}
} finally {
  if(!$Process.HasExited) {Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue}
}


