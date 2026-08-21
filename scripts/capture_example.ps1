#!/usr/bin/env pwsh
<#
.SYNOPSIS
  Launch an EVEngine example and capture a real rendered frame to PNG.

.DESCRIPTION
  Runs a prebuilt `eve.exe` (see -Eve) as `eve run -r scripts/capture_root.nut
  <examples/<Example>>`. The wrapper root script mirrors the engine's standard
  load.nut, waits N frames for the scene to settle, then calls
  `gfx.saveFramePng()` so the engine itself writes the last presented frame
  from the swapchain readback. If the engine-side capture does not produce a
  PNG, the script falls back to capturing the game window's client area via
  Win32 (PrintWindow / CopyFromScreen).

.EXAMPLE
  ./scripts/capture_example.ps1 -Example weather -Out docs/images/weather.png -Frames 240
.PARAMETER Example
  Name of an example directory under examples/ (must contain main.nut).
.PARAMETER Out
  Absolute or repo-relative output PNG path. Default: docs/images/<Example>.png
.PARAMETER Frames
  Number of frames to run before capturing (default 180, ~3s at 60fps).
.PARAMETER WaitSec
  Extra seconds to wait for the process to finish after capture (default 15).
.PARAMETER KeepLogs
  Keep the eve stdout/stderr logs in $env:TEMP for debugging (default off).
.PARAMETER Eve
  Path to eve.exe. Defaults to $env:EVE, the parent EVEngine workspace build,
  or a local build/win32-debug binary.
#>
param(
    [Parameter(Mandatory = $true)][string]$Example,
    [string]$Out,
    [int]$Frames = 180,
    [int]$WaitSec = 15,
    [switch]$KeepLogs,
    [string]$Eve
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

if (-not $Eve) {
    if ($env:EVE) {
        $Eve = $env:EVE
    } elseif (Test-Path "C:\Users\xiaofans\Workspace\Agents\EVEngine\build\win32-debug\src\engine\eve.exe") {
        $Eve = "C:\Users\xiaofans\Workspace\Agents\EVEngine\build\win32-debug\src\engine\eve.exe"
    } else {
        $Eve = Join-Path $RepoRoot "build\win32-debug\src\engine\eve.exe"
    }
}
if (-not (Test-Path $Eve)) { throw "eve.exe not found at $Eve (set -Eve)" }

$GameDir = Join-Path $RepoRoot "examples\$Example"
if (-not (Test-Path (Join-Path $GameDir "main.nut"))) {
    throw "examples\$Example has no main.nut"
}
if (-not $Out) { $Out = Join-Path $RepoRoot "docs\images\$Example.png" }
$Out = [IO.Path]::GetFullPath($Out)
$OutFwd = $Out.Replace('\', '/')

$RootScript = Join-Path $PSScriptRoot "capture_root.nut"
if (-not (Test-Path $RootScript)) { throw "capture_root.nut not found next to this script" }

function Show-CaptureStats([string]$Path) {
    Add-Type -AssemblyName System.Drawing
    $img = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $colors = @{}
        $r = 0; $g = 0; $b = 0; $n = 0
        $stepX = [Math]::Max(1, [int]($img.Width / 40))
        $stepY = [Math]::Max(1, [int]($img.Height / 40))
        for ($x = 0; $x -lt $img.Width; $x += $stepX) {
            for ($y = 0; $y -lt $img.Height; $y += $stepY) {
                $p = $img.GetPixel($x, $y)
                $r += $p.R; $g += $p.G; $b += $p.B; $n++
                $colors["$($p.R),$($p.G),$($p.B)"] = 1
            }
        }
        Write-Host "[stats] $($img.Width)x$($img.Height) sampled=$n unique=$($colors.Count) avgRGB=$([int]($r/$n)),$([int]($g/$n)),$([int]($b/$n))"
    } finally {
        $img.Dispose()
    }
}

$stamp = Get-Date -Format "HHmmssfff"
$stdoutLog = Join-Path $env:TEMP "eve_${Example}_${stamp}.out.log"
$stderrLog = Join-Path $env:TEMP "eve_${Example}_${stamp}.err.log"
$settingsFile = Join-Path $GameDir "capture_settings.nut"
$proc = $null
try {
    # Per-run settings consumed by capture_root.nut (removed afterwards).
    $settings = @"
capture_path <- "$OutFwd";
capture_frame <- $Frames;
capture_tries <- 30;
"@
    Set-Content -Path $settingsFile -Value $settings -Encoding ASCII

    $proc = Start-Process -FilePath $Eve `
        -ArgumentList @("run", "-r", $RootScript, $GameDir) `
        -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $stdoutLog `
        -RedirectStandardError $stderrLog `
        -PassThru

    Write-Host "[capture] running '$Example' ($Frames frames) ..."
    $deadline = (Get-Date).AddSeconds([Math]::Max(30, 20 + $Frames / 30 + $WaitSec))
    $engineCapture = $false
    while ((Get-Date) -lt $deadline) {
        if ($proc.HasExited) {
            # stdout is pipe-buffered: re-read once more after exit to catch the
            # final flush (CAPTURED may land in the file only now).
            $tail = ""
            foreach ($log in @($stdoutLog, $stderrLog)) {
                if (Test-Path $log) {
                    $tail += Get-Content $log -Raw -ErrorAction SilentlyContinue
                }
            }
            if ($tail -match "CAPTURED ") { $engineCapture = $true }
            break
        }
        $tail = ""
        foreach ($log in @($stdoutLog, $stderrLog)) {
            if (Test-Path $log) {
                $tail += Get-Content $log -Raw -ErrorAction SilentlyContinue
            }
        }
        if ($tail -match "CAPTURED ") { $engineCapture = $true; break }
        if ($tail -match "CAPTURE_FAILED") { break }
        Start-Sleep -Milliseconds 500
    }
    if ($engineCapture) {
        # Give the PNG writer a moment to flush the file.
        Start-Sleep -Seconds 1
    }

    # The engine writes the PNG itself; if it exists and is non-empty, treat the
    # capture as successful even when the CAPTURED marker was not observed.
    if ((Test-Path $Out) -and (Get-Item $Out).Length -gt 0) {
        $fi = Get-Item $Out
        if ($fi.Length -eq 0) { throw "screenshot is empty: $Out" }
        Write-Host "[capture] OK (engine frame): $Out ($($fi.Length) bytes)"
        Show-CaptureStats $Out
        return
    }

    # ---- Fallback: capture the game window client area (desktop) ----
    Write-Host "[capture] engine capture unavailable; falling back to window capture ..."
    Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class EveCapture {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT pt);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr hWnd, System.Text.StringBuilder sb, int max);
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
}
"@
    Add-Type -AssemblyName System.Drawing
    [EveCapture]::SetProcessDPIAware() | Out-Null

    $script:targetPid = $proc.Id
    $hwnd = [IntPtr]::Zero
    $deadline2 = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline2 -and -not $proc.HasExited) {
        $script:windows = [System.Collections.Generic.List[IntPtr]]::new()
        $cb = {
            param($h, $l)
            $pid2 = 0
            [EveCapture]::GetWindowThreadProcessId($h, [ref]$pid2) | Out-Null
            if ($pid2 -eq $script:targetPid) { $script:windows.Add($h) }
            return $true
        }
        [EveCapture]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
        foreach ($cand in $script:windows) {
            $r = New-Object EveCapture+RECT
            if (-not [EveCapture]::GetClientRect($cand, [ref]$r)) { continue }
            if (($r.Right - $r.Left) -le 0 -or ($r.Bottom - $r.Top) -le 0) { continue }
            if (-not [EveCapture]::IsWindowVisible($cand)) { continue }
            $cls = New-Object System.Text.StringBuilder 128
            [EveCapture]::GetClassName($cand, $cls, $cls.Capacity) | Out-Null
            if ($cls.ToString() -eq "SDL_app") { $hwnd = $cand; break }
        }
        if ($hwnd -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 300
    }
    if ($hwnd -eq [IntPtr]::Zero) {
        $log = (Get-Content $stderrLog -Raw -ErrorAction SilentlyContinue) + "`n" + (Get-Content $stdoutLog -Raw -ErrorAction SilentlyContinue)
        throw "no SDL window found. log: $log"
    }

    $client = New-Object EveCapture+RECT
    [EveCapture]::GetClientRect($hwnd, [ref]$client) | Out-Null
    $w = $client.Right - $client.Left
    $h = $client.Bottom - $client.Top
    if ($w -le 0 -or $h -le 0) { throw "invalid client rect ${w}x${h}" }

    $dir = Split-Path -Parent $Out
    if ($dir) { New-Item -ItemType Directory -Force -Path $dir | Out-Null }
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    try {
        $gfx = [System.Drawing.Graphics]::FromImage($bmp)
        try {
            $hdc = $gfx.GetHdc()
            $pw = [EveCapture]::PrintWindow($hwnd, $hdc, 3)
            $gfx.ReleaseHdc($hdc)
            if (-not $pw) {
                $origin = New-Object EveCapture+POINT
                [EveCapture]::ClientToScreen($hwnd, [ref]$origin) | Out-Null
                $gfx2 = [System.Drawing.Graphics]::FromImage($bmp)
                try {
                    $gfx2.CopyFromScreen($origin.X, $origin.Y, 0, 0, $bmp.Size,
                        [System.Drawing.CopyPixelOperation]::SourceCopy)
                } finally {
                    $gfx2.Dispose()
                }
            }
        } finally {
            $gfx.Dispose()
        }
        $bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $bmp.Dispose()
    }
    $fi = Get-Item $Out
    if ($fi.Length -eq 0) { throw "screenshot is empty: $Out" }
    Write-Host "[capture] OK (window capture): $Out (${w}x${h}, $($fi.Length) bytes)"
    Show-CaptureStats $Out
} finally {
    if ($proc -and -not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force
        Wait-Process -Id $proc.Id -ErrorAction SilentlyContinue
    }
    Remove-Item $settingsFile -ErrorAction SilentlyContinue
    if (-not $KeepLogs) {
        Remove-Item $stdoutLog, $stderrLog -ErrorAction SilentlyContinue
    } else {
        Write-Host "[debug] logs kept: $stdoutLog / $stderrLog"
    }
}
