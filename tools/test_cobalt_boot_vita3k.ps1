param(
  [string]$Vita3K = "C:\Users\wolff\Documents\Vita3k\Vita3K.exe",
  [string]$TitleId = 'YTVCB0001',
  [string]$Vpk = 'CobaltNativeBootstrapTest.vpk',
  [int]$Seconds = 90,
  [string]$Config = 'vita3k-test.yml',
  [string]$Backend = '',
  [switch]$CaptureFrames,
  [int]$CaptureIntervalMs = 3000,
  [int]$CaptureMax = 40,
  [switch]$CaptureStream,   # every frame (<=30 fps) as raw RGB into frames.rgb under the capture folder
  [switch]$RecordAudio      # player PCM into rec_player.raw (diagnostic)
)
# Runs the isolated Cobalt boot probe in Vita3K and captures, for this run
# only: cobalt_boot.log, starboard15.log and vita3k.log. Only the Vita3K
# process started here is ever terminated.
$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$VpkPath = Join-Path $Root "build-starboard15\$Vpk"
$Profile = Join-Path $env:APPDATA 'Vita3K\Vita3K'
$DataDir = Join-Path $Profile 'ux0\data\youtubevita'
$AppSelf = Join-Path $Profile "ux0\app\$TitleId\eboot.bin"
$Cfg = Join-Path $Root "experiments\cobalt\vita\$Config"
$EmuLog = Join-Path (Split-Path $Vita3K) 'vita3k.log'
if (!(Test-Path $VpkPath)) { throw "Build $Vpk first" }
if (!(Test-Path $AppSelf)) { throw "$TitleId not installed in Vita3K" }
# Other Vita3K sessions (the user's own games) are left alone; only refuse if
# this same test title is already running.
$mine = Get-CimInstance Win32_Process -Filter "Name='Vita3K.exe'" |
  Where-Object { $_.CommandLine -match "-r\s+$TitleId" }
if ($mine) { throw "$TitleId is already running in Vita3K (PID $($mine.ProcessId))" }
# Test runs keep the diagnostic YTV_* log lines.
[IO.File]::WriteAllText((Join-Path $DataDir 'verbose.txt'), '1')
# Diagnostic cutoffs must be disabled for a real boot.
foreach ($f in 'event_cutoff.txt', 'inner_cutoff.txt', 'init_cutoff.txt', 'ctor_cutoff.txt') {
  $p = Join-Path $DataDir $f
  if (Test-Path $p) { [IO.File]::WriteAllText($p, '0') }
}
# Sync every packaged file whose size/content changed into the installed
# title (eboot.bin, fonts, certificates...). sce_sys is left to the installer.
$Py = "import zipfile,pathlib,zlib;app=pathlib.Path(r'$AppSelf').parent;z=zipfile.ZipFile(r'$VpkPath');n=0`nfor i in z.infolist():`n if i.is_dir() or i.filename.startswith('sce_sys/'):continue`n o=app/i.filename`n if o.exists() and o.stat().st_size==i.file_size and zlib.crc32(o.read_bytes())==i.CRC:continue`n o.parent.mkdir(parents=True,exist_ok=True);o.write_bytes(z.read(i));n+=1`nprint('synced',n,'files')"
& python -c $Py
if ($LASTEXITCODE -ne 0) { throw 'Failed to update eboot.bin' }

$Logs = @{ boot = (Join-Path $DataDir 'cobalt_boot.log'); sb = (Join-Path $DataDir 'starboard15.log') }
$Start = @{}
foreach ($k in $Logs.Keys) { $Start[$k] = if (Test-Path $Logs[$k]) { (Get-Item $Logs[$k]).Length } else { 0 } }

$RecFlag = Join-Path $DataDir 'record_audio.txt'
$RecRaw = Join-Path $DataDir 'rec_player.raw'
if (Test-Path $RecRaw) { Remove-Item $RecRaw -Force }
if ($RecordAudio) { [IO.File]::WriteAllText($RecFlag, '1') }
elseif (Test-Path $RecFlag) { Remove-Item $RecFlag -Force }
if ($CaptureStream) { $CaptureFrames = [switch]$true }

$Stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$Out = Join-Path $Root "tools\capture\cobalt-boot-$Stamp"
New-Item -ItemType Directory -Force $Out | Out-Null

# Optional: real framebuffer capture (Windows screen capture of the OpenGL
# window shows white). The env var is inherited only by the Vita3K started
# here, and the SwapBuffers hook DLL is injected only into that process.
if ($CaptureFrames) {
  $env:VITA3K_CAPTURE_DIR = Join-Path $Out 'frames'
  $env:VITA3K_CAPTURE_INTERVAL_MS = "$CaptureIntervalMs"; $env:VITA3K_CAPTURE_MAX = "$CaptureMax"
  $env:VITA3K_CAPTURE_STREAM = $(if ($CaptureStream) { '1' } else { '0' })
  New-Item -ItemType Directory -Force $env:VITA3K_CAPTURE_DIR | Out-Null
}
$Proc = Start-Process -FilePath $Vita3K -WorkingDirectory (Split-Path $Vita3K) `
  -ArgumentList (@('-c', $Cfg) + $(if ($Backend) { @('--backend-renderer', $Backend) } else { @() }) + @('-r', $TitleId)) -PassThru
if ($CaptureFrames) {
  Remove-Item Env:VITA3K_CAPTURE_DIR,Env:VITA3K_CAPTURE_INTERVAL_MS,Env:VITA3K_CAPTURE_MAX,Env:VITA3K_CAPTURE_STREAM -ErrorAction SilentlyContinue
  Start-Sleep -Seconds 3
  & python (Join-Path $PSScriptRoot 'opengl_capture\inject_dll.py') $Proc.Id (Join-Path $PSScriptRoot 'opengl_capture\capture_swap.dll')
}
try {
  $deadline = (Get-Date).AddSeconds($Seconds)
  while ((Get-Date) -lt $deadline -and !$Proc.HasExited) {
    Start-Sleep -Seconds 2
    try {
      $fs = [IO.File]::Open($Logs.boot, 'Open', 'Read', 'ReadWrite')
      $fs.Seek([int64]$Start.boot, 'Begin') | Out-Null
      $tail = (New-Object IO.StreamReader($fs)).ReadToEnd(); $fs.Close()
      if ($tail -match 'RESULT (PASS|FAIL)') { break }
    } catch {}
  }
} finally {
  $SelfExit = $Proc.HasExited
  $Elapsed = [int]((Get-Date) - $Proc.StartTime).TotalSeconds
  if (!$Proc.HasExited) { Stop-Process -Id $Proc.Id -Force -ErrorAction SilentlyContinue }
}
Start-Sleep -Seconds 1
foreach ($k in $Logs.Keys) {
  $fs = [IO.File]::Open($Logs[$k], 'Open', 'Read', 'ReadWrite')
  $fs.Seek([int64]$Start[$k], 'Begin') | Out-Null
  $text = (New-Object IO.StreamReader($fs)).ReadToEnd(); $fs.Close()
  [IO.File]::WriteAllText((Join-Path $Out "$k.log"), $text)
}
if ($RecordAudio) {
  if (Test-Path $RecRaw) { Copy-Item $RecRaw (Join-Path $Out 'rec_player.raw') }
  Remove-Item $RecFlag -Force -ErrorAction SilentlyContinue
}
if (Test-Path $EmuLog) { Copy-Item $EmuLog (Join-Path $Out 'vita3k.log') }
$Code = if ($SelfExit) { $Proc.ExitCode } else { "killed" }
Write-Output "CAPTURE=$Out SELF_EXIT=$SelfExit EXIT_CODE=$Code ELAPSED=${Elapsed}s"
