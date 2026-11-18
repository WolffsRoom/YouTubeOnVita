param([string]$TitleId = 'YTVCB0001', [string]$Out = '')
# Captures the window of the Vita3K process running $TitleId (only that one)
# using PrintWindow(PW_RENDERFULLCONTENT), which works for OpenGL windows.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class W32 {
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
}
"@
$p = Get-CimInstance Win32_Process -Filter "Name='Vita3K.exe'" |
  Where-Object { $_.CommandLine -match "-r\s+$TitleId" } | Select-Object -First 1
if (!$p) { throw "No Vita3K running $TitleId" }
$h = (Get-Process -Id $p.ProcessId).MainWindowHandle
if ($h -eq [IntPtr]::Zero) { throw 'Vita3K window handle not available' }
$r = New-Object W32+RECT
[void][W32]::GetWindowRect($h, [ref]$r)
$w = $r.R - $r.L; $hh = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[void][W32]::PrintWindow($h, $dc, 2)
$g.ReleaseHdc($dc); $g.Dispose()
if (!$Out) { $Out = Join-Path $PSScriptRoot ("capture\vita3k-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.png') }
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
Write-Output "SCREENSHOT=$Out ${w}x${hh}"
