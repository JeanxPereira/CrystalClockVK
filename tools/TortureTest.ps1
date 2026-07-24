$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$log = Join-Path $root 'torture.log'
Start-Process -FilePath (Join-Path $root 'bin\CrystalClockVK.exe') -WorkingDirectory (Join-Path $root 'bin') -RedirectStandardOutput $log -RedirectStandardError (Join-Path $root 'torture.err.log')
Start-Sleep 5
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class TT {
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int hh, bool r);
}
'@
$p = Get-Process CrystalClockVK -ErrorAction SilentlyContinue
if (-not $p) { 'DEAD MID-TORTURE'; exit 1 }
1..2 | ForEach-Object {
  [TT]::ShowWindow($p.MainWindowHandle, 6) | Out-Null; Start-Sleep 2
  [TT]::ShowWindow($p.MainWindowHandle, 9) | Out-Null; Start-Sleep 2
}
[TT]::MoveWindow($p.MainWindowHandle, 80, 80, 1500, 850, $true) | Out-Null; Start-Sleep 2
[TT]::MoveWindow($p.MainWindowHandle, 120, 120, 800, 600, $true) | Out-Null; Start-Sleep 2
$alive = Get-Process CrystalClockVK -ErrorAction SilentlyContinue
if ($alive) { 'SURVIVED'; $null = $alive.CloseMainWindow() } else { 'DEAD MID-TORTURE'; exit 1 }
Start-Sleep 4
$bad = @(Select-String -Path $log, (Join-Path $root 'torture.err.log') -Pattern 'ERROR|Assertion|Render loop error' -ErrorAction SilentlyContinue).Count
"BadLines: $bad"
if (Get-Process CrystalClockVK -ErrorAction SilentlyContinue) { 'STILL UP'; exit 1 }
'CLEAN EXIT'
if ($bad -ne 0) { exit 1 }
exit 0
