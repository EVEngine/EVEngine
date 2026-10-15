# Two disposable, real git worktrees are required. Never use an active build tree.
param(
    [Parameter(Mandatory)][string]$WorktreeA,
    [Parameter(Mandatory)][string]$WorktreeB,
    [Parameter(Mandatory)][string]$Ccache,
    [Parameter(Mandatory)][string]$EvidenceDir,
    [switch]$TrimFilePaths
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$roots = @((Resolve-Path $WorktreeA).Path, (Resolve-Path $WorktreeB).Path)
if ($roots[0] -eq $roots[1]) { throw 'Two distinct worktrees are required' }
New-Item -ItemType Directory $EvidenceDir -Force | Out-Null
$EvidenceDir = (Resolve-Path $EvidenceDir).Path
$results = @()
$storage = @()
function Run($label, $exe, $arguments) {
    $out = & $exe @arguments 2>&1
    $out | Set-Content "$EvidenceDir/$label.log" -Encoding utf8
    if ($LASTEXITCODE -ne 0) { throw "$label failed; see $EvidenceDir/$label.log" }
}
function Check($case, $actual, $expected) {
    $script:results += [pscustomobject]@{ Case=$case; Actual=$actual; Expected=$expected; Passed=($actual -eq $expected) }
    $script:results | ConvertTo-Json | Set-Content "$EvidenceDir/results.json" -Encoding utf8
    if ($actual -ne $expected) { throw "$case failed: $actual != $expected" }
}
$main = @'
#include "common.h"
#include <cstdio>
#include <string>
#include <vector>
int other();
int main() { std::vector<std::string> v{"probe"}; std::printf("%d %d %s\n", VALUE + DELTA, other(), v[0].c_str()); }
'@
if ($TrimFilePaths) {
    $main = $main.Replace('int main() {', 'int main() { if (std::string(__FILE__) != "main.cpp" || std::string(cacheHeaderPath()) != "common.h") return 9;')
}
function WriteHeader($directory, $value) {
    $header = "#pragma once`n#define VALUE $value"
    if ($TrimFilePaths) { $header += "`ninline const char* cacheHeaderPath() { return __FILE__; }" }
    $header | Set-Content "$directory/common.h" -Encoding utf8
}
$project = @'
cmake_minimum_required(VERSION 3.25)
include("@HELPER@")
eve_prepare_ccache_hardlink()
project(CacheProbe LANGUAGES C CXX)
eve_finish_ccache_hardlink()
add_executable(probe main.cpp other.cpp)
add_dependencies(probe eve_ccache_source_map)
target_compile_features(probe PRIVATE cxx_std_20)
target_link_options(probe PRIVATE /INCREMENTAL:NO /DEBUG:FULL)
'@
$project = $project.Replace('@HELPER@', ($repo.Replace('\','/') + '/cmake/ccache_hardlink.cmake'))
$src = @(); $build = @()
foreach ($root in $roots) {
    Run "git-$($src.Count)" git @('-C',$root,'rev-parse','--show-toplevel')
    $src += "$root/build/ccache-verification-src"
    $build += "$root/build/ccache-verification-out"
    New-Item -ItemType Directory $src[-1] -Force | Out-Null
    $project | Set-Content "$($src[-1])/CMakeLists.txt" -Encoding utf8
    $main.Replace('DELTA','0') | Set-Content "$($src[-1])/main.cpp" -Encoding utf8
    "#include `"common.h`"`nint other() { return VALUE; }" | Set-Content "$($src[-1])/other.cpp" -Encoding utf8
    WriteHeader $src[-1] 10
    Run "configure-$($src.Count)" "$repo/cmake/with-msvc.cmd" @('cmake','-S',$src[-1],'-B',$build[-1],'-G','Ninja','-DCMAKE_BUILD_TYPE=Debug',"-DEVENGINE_CCACHE_EXECUTABLE=$Ccache","-DEVENGINE_CCACHE_DIR=$EvidenceDir/cache", "-DEVENGINE_CCACHE_TRIM_FILE_PATHS=$($TrimFilePaths.IsPresent)")
}
Start-Sleep -Seconds 2
foreach ($i in 0..1) { Run "baseline-$i" cmake @('--build',$build[$i],'--target','probe','--parallel','2','--verbose') }
$obj = @("$($build[0])/CMakeFiles/probe.dir/main.cpp.obj", "$($build[1])/CMakeFiles/probe.dir/main.cpp.obj")
$ids = foreach ($path in $obj) { (& fsutil file queryfileid $path) -join '' }
Check 'baseline NTFS file identity' $ids[1] $ids[0]
& fsutil hardlink list $obj[0] | Set-Content "$EvidenceDir/hardlinks.txt" -Encoding utf8
$baselineHash = (Get-FileHash $obj[1]).Hash
Start-Sleep -Seconds 2
$main.Replace('DELTA','7') | Set-Content "$($src[0])/main.cpp" -Encoding utf8
Run 'cpp-a' cmake @('--build',$build[0],'--verbose')
Check 'A CPP changed' ((& "$($build[0])/probe.exe") -join '') '17 10 probe'
Check 'B object unchanged' (Get-FileHash $obj[1]).Hash $baselineHash
Check 'B executable unchanged' ((& "$($build[1])/probe.exe") -join '') '10 10 probe'
Start-Sleep -Seconds 2
WriteHeader $src[1] 20
Run 'header-b' cmake @('--build',$build[1],'--verbose')
Check 'B own public header' ((& "$($build[1])/probe.exe") -join '') '20 20 probe'
Run 'deps-b' ninja @('-C',$build[1],'-t','deps')
Start-Sleep -Seconds 2
WriteHeader $src[0] 30
Run 'header-a' cmake @('--build',$build[0],'--verbose')
Check 'A own public header' ((& "$($build[0])/probe.exe") -join '') '37 30 probe'
foreach ($mode in @('cold','warm','divergent')) {
    foreach ($round in 1..5) {
        $jobs = @()
        foreach ($i in 0..1) {
            $value=100+$round
            if ($mode -eq 'divergent' -and $i -eq 1) { $value=200+$round }
            WriteHeader $src[$i] $value
            # Warm rounds reproduce cold input exactly, including comments.
            $main.Replace('DELTA','0') + "`n// round $round" | Set-Content "$($src[$i])/main.cpp" -Encoding utf8
        }
        Start-Sleep -Seconds 2
        foreach ($i in 0..1) {
            $p=Start-Process cmake -ArgumentList @('--build',('"'+$build[$i]+'"'),'--parallel','2','--verbose') -WindowStyle Hidden -RedirectStandardOutput "$EvidenceDir/$mode-$round-$i.log" -RedirectStandardError "$EvidenceDir/$mode-$round-$i.err" -PassThru
            $jobs += $p
        }
        foreach ($i in 0..1) {
            if (!$jobs[$i].WaitForExit(60000)) { throw 'Concurrent build timed out' }
            if ($jobs[$i].ExitCode -ne 0) { throw 'Concurrent build failed' }
            $value=100+$round
            if ($mode -eq 'divergent' -and $i -eq 1) { $value=200+$round }
            Check "$mode/$round/$i" ((& "$($build[$i])/probe.exe") -join '') "$value $value probe"
        }
        foreach ($file in @('main.cpp.obj','other.cpp.obj')) {
            $fileIds = foreach ($i in 0..1) {
                (& fsutil file queryfileid "$($build[$i])/CMakeFiles/probe.dir/$file") -join ''
            }
            $storage += [pscustomobject]@{ Mode=$mode; Round=$round; Object=$file; FileIdA=$fileIds[0]; FileIdB=$fileIds[1]; Shared=($fileIds[0] -eq $fileIds[1]) }
            $storage | ConvertTo-Json | Set-Content "$EvidenceDir/concurrent-storage.json" -Encoding utf8
            if ($mode -eq 'warm') { Check "warm storage/$round/$file" $fileIds[0] $fileIds[1] }
        }
        Write-Host "$mode round $round passed"
    }
}
# Force a cache hit produced by A before testing a real breakpoint in B.
foreach ($i in 0..1) {
    $main.Replace('DELTA','0') + "`n// debug mapping" | Set-Content "$($src[$i])/main.cpp" -Encoding utf8
    WriteHeader $src[$i] 77
}
Start-Sleep -Seconds 2
foreach ($i in 0..1) { Run "debug-build-$i" cmake @('--build',$build[$i],'--verbose') }
$ids = foreach ($path in $obj) { (& fsutil file queryfileid $path) -join '' }
Check 'debug cache hit NTFS identity' $ids[1] $ids[0]
$map = Get-Content "$($build[1])/ccache/sourceFileMap.json" -Raw | ConvertFrom-Json
$producer=$src[0].Replace('\','/')
Check 'generated debugger map' $map.$producer $src[1].Replace('\','/')
foreach ($tool in @('debugbreak','debugpaths')) {
    Run "compile-$tool" "$repo/cmake/with-msvc.cmd" @('cl','/nologo','/std:c++20','/EHsc',"$repo/scripts/tests/ccache_windows/$tool.cpp","/Fe:$EvidenceDir/$tool.exe","/Fo:$EvidenceDir/$tool.obj",'/link','dbghelp.lib')
}
Run 'pdb-path-b' "$EvidenceDir/debugpaths.exe" @("$($build[1])/probe.exe")
$line = Get-Content "$EvidenceDir/pdb-path-b.log" | Where-Object { $_ -match '^MAIN (.*):([0-9]+)$' } | Select-Object -Last 1
if ($line -notmatch '^MAIN (.*):([0-9]+)$') { throw 'PDB main source line missing' }
$bakedPath = $Matches[1].Replace('\','/')
$mapping = $map.PSObject.Properties | Where-Object { $bakedPath.StartsWith($_.Name.Replace('\','/')) } | Select-Object -First 1
if (!$mapping) { throw "Generated sourceFileMap does not cover PDB path: $bakedPath" }
Run 'mapped-breakpoint-b' "$EvidenceDir/debugbreak.exe" @("$($build[1])/probe.exe", $mapping.Name.Replace('/','\'), $mapping.Value.Replace('/','\'))
Check 'real mapped breakpoint' ((Get-Content "$EvidenceDir/mapped-breakpoint-b.log" -Raw).Contains('BREAKPOINT')) $true
Run 'no-op-b' cmake @('--build',$build[1],'--verbose')
Check 'no-op does not compile' ((Get-Content "$EvidenceDir/no-op-b.log" -Raw).Contains('main.cpp.obj')) $false
$env:CCACHE_DIR="$EvidenceDir/cache"
Run 'cache-stats' $Ccache @('--show-stats','--verbose')
Write-Host "All $($results.Count) assertions passed. Evidence: $EvidenceDir"
