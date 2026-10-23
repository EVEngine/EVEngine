param(
    [Parameter(Mandatory = $true)][string]$Root,
    [string]$Eve = "build/road-parity/host-profile/src/engine/eve.exe",
    [string]$GamePattern = "*"
)
$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
Push-Location $repoRoot
try {
    $runtimeJson = & python -X utf8 -c 'import sys,json;sys.path.insert(0,"scripts");import road_parity;from pathlib import Path;print(json.dumps(road_parity.renderer_runtime_hashes(Path(sys.argv[1]))))' $Eve
    if ($LASTEXITCODE -ne 0) { throw "Cannot inventory capture runtime" }
    $runtime = $runtimeJson | ConvertFrom-Json
    foreach ($game in (Get-ChildItem (Join-Path $Root "games") -Directory -Filter $GamePattern | Sort-Object Name)) {
        $inputs = @{}
        Get-ChildItem $game.FullName -File | Where-Object {
            $_.Name -ne "mesh_data.nut" -and $_.Name -notlike "frame*" -and $_.Extension -in @(".nut", ".glb", ".png", ".spv")
        } | ForEach-Object {
            $inputs[$_.Name] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        }
        $log = Join-Path $game.FullName "capture.log"
        & (Join-Path $repoRoot "scripts/capture_example.ps1") -Game $game.FullName -Eve $Eve -Frames 24 -Out (Join-Path $game.FullName "frame.png") -KeepLogs *> $log
        if (-not (Select-String -LiteralPath $log -Pattern '\[capture\] OK' -Quiet)) {
            throw "Capture failed: $($game.Name)"
        }
        @{
            schema = "eve.tests.bundled-capture-inputs"; version = 1; inputs = $inputs; runtime = $runtime
            frameSha256 = (Get-FileHash -LiteralPath (Join-Path $game.FullName "frame.png") -Algorithm SHA256).Hash.ToLowerInvariant()
        } | ConvertTo-Json -Depth 10 | Set-Content (Join-Path $game.FullName "capture-inputs.json") -Encoding utf8
        Write-Output "captured: $($game.Name)"
    }
} finally {
    Pop-Location
}
