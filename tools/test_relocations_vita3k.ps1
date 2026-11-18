param([string]$Vita3K="C:\Users\wolff\Documents\Vita3k\Vita3K.exe",
      [int]$ExpectedChecks=25)
$ErrorActionPreference='Stop'
$Root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$Vpk=Join-Path $Root 'build-starboard15\CobaltEvergreenRelocations.vpk'
$App='YTVRL0001'
$VitaProfile=Join-Path $env:APPDATA 'Vita3K\Vita3K'
$Log=Join-Path $VitaProfile 'ux0\data\youtubevita\relocations.log'
$EmuCfg=Join-Path $Root 'experiments\cobalt\vita\vita3k-test.yml'

if(!(Test-Path $Vpk)){throw "Build missing: $Vpk"}
if(!(Test-Path $Vita3K)){throw "Vita3K missing: $Vita3K"}
if(!(Test-Path $EmuCfg)){throw 'Vita3K test configuration missing'}
if(Get-Process Vita3K -ErrorAction SilentlyContinue){
  throw 'Vita3K is running; this script does not interrupt existing sessions'
}
$Self=Join-Path $VitaProfile "ux0\app\$App\eboot.bin"
$So=Join-Path $VitaProfile "ux0\app\$App\libcobalt.so"
if(!(Test-Path $Self) -or !(Test-Path $So)){
  throw "Install $Vpk through Vita3K before the first automated regression"
}
$Py="import zipfile,pathlib;v=pathlib.Path(r'$Vpk');o=pathlib.Path(r'$Self');o.write_bytes(zipfile.ZipFile(v).read('eboot.bin'))"
& python -c $Py
if($LASTEXITCODE -ne 0){throw 'Could not update test eboot.bin'}

# Capture only new log data, never accept a stale PASS.
$PreviousSize=if(Test-Path $Log){(Get-Item $Log).Length}else{0}
$Proc=Start-Process -FilePath $Vita3K -WorkingDirectory (Split-Path $Vita3K) -ArgumentList @('-c',$EmuCfg,'-r',$App) -PassThru
try {
  $ok=$false
  for($i=0;$i -lt 60;$i++) {
    Start-Sleep -Milliseconds 350
    if(!(Test-Path $Log)){continue}
    $all=[IO.File]::ReadAllText($Log)
    $increment=$all.Substring([Math]::Min([int]$PreviousSize,$all.Length))
    $begin=$increment.LastIndexOf('START Evergreen armv7 RAM-only relocation validation')
    if($begin -lt 0){continue}
    $tail=$increment.Substring($begin)
    if($tail -match 'RESULT (PASS|FAIL) \(([0-9]+) passed, ([0-9]+) failed\)') {
      $status=$Matches[1];$passed=[int]$Matches[2];$failed=[int]$Matches[3]
      if($status -ne 'PASS' -or $failed -ne 0 -or $passed -ne $ExpectedChecks){
        throw "Unexpected result: $status passed=$passed failed=$failed expected=$ExpectedChecks"
      }
      foreach($evidence in @(
        'RELOCATED_AND_DEFERRED 64907 211',
        'REAL_STARBOARD_EXPORTS_LINKED 211 0',
        'COBALT_TEXT_RX_PROTECTION 0 24674304',
        'TRUSTED_ARM_EXEC_RETURN 42 8482816',
        'ELF_INIT_ARRAY 74 1',
        'COBALT_CODE_EXECUTION_NOT_ATTEMPTED'
      )){
        if(!$tail.Contains($evidence)){throw "Missing evidence from current run: $evidence"}
      }
      Write-Output "VITA3K_RELOCATION_TEST=PASS CHECKS=$passed ERRORS=$failed LINKED=211 MISSING=0"
      $ok=$true
      break
    }
  }
  if(!$ok){throw 'No fresh relocation test result within the defined test window'}
} finally {
  if(!$Proc.HasExited){Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue}
}

