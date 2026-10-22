#!/usr/bin/env pwsh
<#
.SYNOPSIS
  Launch an EVEngine example and capture a real rendered frame to PNG.

.DESCRIPTION
  Runs a prebuilt `eve.exe` (see -Eve) with a temporary root composed from
  src/scripts/load.nut and scripts/capture_root.nut. The capture hook runs inside the repository standard
  load.nut, waits N frames for the scene to settle, then calls
  `gfx.saveFramePng()` so the engine itself writes the last presented frame
  from the swapchain readback. If the engine-side capture does not produce a
  PNG, the script fails and preserves the previous output.

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
.PARAMETER Setup
  Optional script run after normal initialization to select a reproducible scene preset.
.PARAMETER Eve
  Path to eve.exe. Defaults to $env:EVE, the parent EVEngine workspace build,
  or a local build/win32-debug binary.
.PARAMETER Game
  Explicit game directory instead of an examples/ name; useful for generated test fixtures.
#>
param(
    [Parameter(Mandatory = $true, ParameterSetName = "Example")][string]$Example,
    [Parameter(Mandatory = $true, ParameterSetName = "Game")][string]$Game,
    [string]$Out,
    [int]$Frames = 180,
    [int]$WaitSec = 15,
    [switch]$KeepLogs,
    [string]$Eve,
    [string]$Setup
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

$GameDir = if ($Game) { (Resolve-Path -LiteralPath $Game).Path } else { Join-Path $RepoRoot "examples\$Example" }
if ($Game) { $Example = Split-Path -Leaf $GameDir }
if (-not (Test-Path (Join-Path $GameDir "main.nut"))) {
    throw "examples\$Example has no main.nut"
}
if (-not $Out) { $Out = Join-Path $RepoRoot "docs\images\$Example.png" }
$Out = [IO.Path]::GetFullPath($Out)
$FinalOut = $Out
$Out = "$Out.capture-$([Guid]::NewGuid().ToString('N')).png"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Out) | Out-Null
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
$settingsFile = Join-Path $env:TEMP "eve_capture_settings_$stamp.nut"
$setupPath = ""
if ($Setup) { $setupPath = (Resolve-Path -LiteralPath $Setup).Path.Replace('\', '/') }
$proc = $null
$generatedRoot = Join-Path $env:TEMP "eve_capture_root_$stamp.nut"
$originalPath = $env:PATH
try {
    # SHARED builds place link-group DLLs at the build root.
    $engineDir = Split-Path -Parent ([IO.Path]::GetFullPath($Eve))
    $buildRoot = Split-Path -Parent (Split-Path -Parent $engineDir)
    $env:PATH = "$engineDir;$buildRoot;$originalPath"
    # Isolated run settings; existing example capture_settings.nut stays intact.
    $settings = @"
capture_path <- "$OutFwd";
capture_frame <- $Frames;
capture_tries <- 30;
capture_setup <- "$setupPath";
"@
    Set-Content -Path $settingsFile -Value $settings -Encoding utf8

    # Compose the standard startup with one post-frame hook, rather than
    # maintaining a separate copy of module/input/lifecycle initialization.
    $startup = Get-Content (Join-Path $RepoRoot "src/scripts/load.nut") -Raw -Encoding utf8
    $loopMarker = "while (eve_frame()) {"
    if (($startup.Split(@($loopMarker), [StringSplitOptions]::None)).Count -ne 2) {
        throw "standard load.nut must contain exactly one desktop frame loop"
    }
    $hookPath = $RootScript.Replace('\', '/')
    $settingsPath = $settingsFile.Replace('\', '/')
    $startup = $startup.Replace($loopMarker, "dofile(`"$settingsPath`");`n        dofile(`"$hookPath`");`n        $loopMarker`n            if (capture_after_frame()) break;")
    Set-Content -LiteralPath $generatedRoot -Value $startup -Encoding utf8

    $proc = Start-Process -FilePath $Eve `
        -ArgumentList "run -r `"$generatedRoot`" `"$GameDir`"" `
        -WorkingDirectory $RepoRoot `
        -RedirectStandardOutput $stdoutLog `
        -RedirectStandardError $stderrLog `
        -WindowStyle Hidden `
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

    # Require the engine marker and a fresh file; never reuse an old screenshot.
    $captureLog = (Get-Content $stderrLog -Raw -ErrorAction SilentlyContinue) + "`n" + (Get-Content $stdoutLog -Raw -ErrorAction SilentlyContinue)
    if ($captureLog -match "(?im)(frame error:|update error:|render error:|present error:|eve_init failed:|main.nut failed:|failed to initialize:|\[crash\])") {
        throw "example reported a runtime error: $captureLog"
    }
    if ($engineCapture -and (Test-Path $Out) -and (Get-Item $Out).Length -gt 0) {
        $fi = Get-Item $Out
        if ($fi.Length -eq 0) { throw "screenshot is empty: $Out" }
        Write-Host "[capture] OK (engine frame): $Out ($($fi.Length) bytes)"
        Show-CaptureStats $Out
        Move-Item -LiteralPath $Out -Destination $FinalOut -Force
        return
    }

    $log = (Get-Content $stderrLog -Raw -ErrorAction SilentlyContinue) + "`n" + (Get-Content $stdoutLog -Raw -ErrorAction SilentlyContinue)
    throw "engine frame capture failed for $Example. log: $log"
} finally {
    $env:PATH = $originalPath
    if ($proc -and -not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force
        Wait-Process -Id $proc.Id -ErrorAction SilentlyContinue
    }
    Remove-Item -LiteralPath $generatedRoot -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $Out -ErrorAction SilentlyContinue
    Remove-Item $settingsFile -ErrorAction SilentlyContinue
    if (-not $KeepLogs) {
        Remove-Item $stdoutLog, $stderrLog -ErrorAction SilentlyContinue
    } else {
        Write-Host "[debug] logs kept: $stdoutLog / $stderrLog"
    }
}
