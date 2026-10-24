param([Parameter(Mandatory = $true)][string]$BuildDir)
$ErrorActionPreference = 'Stop'

$cache = Get-Content "$BuildDir/CMakeCache.txt" -Raw
if ($cache -notmatch '(?m)^EVENGINE_COMPILER_CACHE:STRING=SCCACHE\r?$') {
    throw 'Windows Debug must configure EVENGINE_COMPILER_CACHE=SCCACHE'
}
$compilerMatch = [regex]::Match($cache, '(?m)^CMAKE_CXX_COMPILER:[^=]+=(.+)\r?$')
if (-not $compilerMatch.Success) { throw 'Configured C++ compiler is missing' }
$compiler = $compilerMatch.Groups[1].Value.Trim()
$rules = (Get-Content "$BuildDir/build.ninja" -Raw) + (Get-Content "$BuildDir/CMakeFiles/rules.ninja" -Raw)
if ($rules -notmatch 'sccache' -or $rules -notmatch '/Z7\b') {
    throw 'Generated Windows compile rules must use sccache and embedded debug information (/Z7)'
}

function Get-CacheHits {
    $json = & sccache --show-stats --stats-format=json
    if ($LASTEXITCODE -ne 0) { throw 'Cannot read sccache statistics' }
    $stats = ($json -join "`n") | ConvertFrom-Json
    $counts = $stats.stats.cache_hits.counts
    if ($null -eq $counts) { throw 'sccache statistics have no cache hit counters' }
    [long]$total = 0
    foreach ($entry in $counts.PSObject.Properties) { $total += [long]$entry.Value }
    return $total
}

# Exercise the compiler selected by the real engine build, including its wrapper.
# Delete the output between identical compiles so the second must restore it.
$probe = New-Item -ItemType Directory -Force "$BuildDir/cache-probe"
$source = Join-Path $probe.FullName 'probe.cpp'
$object = Join-Path $probe.FullName 'probe.obj'
Set-Content -Encoding ascii $source 'int evengine_cache_probe() { return 42; }'
# PRs intentionally cannot populate the shared GHA backend. Use a separate
# local cache/server for this probe so a cold read-only PR cannot fail it.
$env:SCCACHE_GHA_ENABLED = 'false'
$env:SCCACHE_DIR = Join-Path $probe.FullName 'cache'
$env:SCCACHE_SERVER_PORT = '4227'
& sccache --start-server
if ($LASTEXITCODE -ne 0) { throw 'Cannot start the isolated compiler cache probe' }
try {
    $before = Get-CacheHits
    foreach ($attempt in 1..2) {
        Remove-Item $object -ErrorAction SilentlyContinue
        & sccache $compiler /nologo /Z7 /c $source "/Fo$object"
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path $object)) {
            throw "Compiler cache probe $attempt failed"
        }
    }
    $after = Get-CacheHits
    if ($after -le $before) { throw 'Identical Windows compiles did not produce a cache hit' }
    Write-Host "Windows compiler cache verified: $($after - $before) cache hit(s)"
} finally {
    & sccache --stop-server
}
