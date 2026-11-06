# Integration test build measurement

The final physical test organization was compared with Git baseline `20394d720951b2aa13f035dd9b16104c3de3096e` on one Windows x64 host using MSVC 19.51, Ninja, Debug, SHARED modules and domain-split test executables. Four successful cold test builds ran in baseline/final/final/baseline (ABBA) order at four compiler jobs. Both cohorts include the complete test target closure, so moving cases into unit tests is included in the cost.

## Observations

- Run 1 (baseline): 210.905 s; 500 OBJ / 2,392,489,349 bytes; 25 main PDB / 1,059,835,904 bytes; 25 EXE / 290,376,704 bytes.
- Run 2 (final): 195.398 s; 479 OBJ / 2,352,430,711 bytes; 25 main PDB / 1,054,126,080 bytes; 25 EXE / 291,266,560 bytes.
- Run 3 (final): 195.887 s; 479 OBJ / 2,352,430,711 bytes; 25 main PDB / 1,054,150,656 bytes; 25 EXE / 291,266,560 bytes.
- Run 4 (baseline): 200.063 s; 500 OBJ / 2,392,489,349 bytes; 25 main PDB / 1,059,885,056 bytes; 25 EXE / 290,376,704 bytes.

For two observations, the median is their arithmetic mean. Exact raw values, source snapshot hashes and launcher-bypass hashes are in [the sanitized measurement data](integration-build-measurement.json).

- Compile/link wall time (seconds): 205.484 → 195.642 (-4.789%).
- All test OBJ bytes: 2,392,489,349 → 2,352,430,711 (-1.674%).
- Main test linker PDB bytes: 1,059,860,480 → 1,054,138,368 (-0.540%).
- Main test EXE bytes: 290,376,704 → 291,266,560 (+0.306%).
- All rebuilt test PDB bytes (including helper): 1,061,429,248 → 1,055,707,136 (-0.539%).

Test object count fell from 500 to 479. Main test executable size increased by 889,856 bytes (+0.306%); this is an explicit size tradeoff, so these measurements do not establish that every artifact shrank. The main PDB scope is the 25 `unit_test_*` executables. The secondary PDB total includes one additional helper PDB (26 files). PDB sizes varied slightly between repetitions. ILK files were cold-cleaned but their final sizes were not collected.

## Controls and scope

Baseline and final snapshots were materialized at the same physical source path and built in the same build directory. Each snapshot contains the matching test sources, headers, metadata and supporting CMake/scripts; test assets stayed fixed. The generated source hashes are recorded per run and match within each cohort. Configuration and warm-up are excluded from the measured intervals.

Before each timed build, all test-directory OBJ, PDB and ILK files and `unit_test*.exe` files were removed. The timed command compiled and linked every discovered main test target (25 targets), including unit migrations and real helper implementations. Production and third-party dependencies were warmed first. Timed logs reject production compile/link steps, and production OBJ/LIB/DLL size/timestamp checks found no changes in all four runs. This is a cold test-artifact build, not a cold operating-system file-cache or clean-engine build.

Compiler caching was bypassed by removing only the sccache `LAUNCHER` wrappers from generated `build.ninja`. Compiler, `FLAGS` and linker switches were checked unchanged before and after that edit in every run. The CMake cache retained its `AUTO` launcher setting. Setting `SCCACHE_DISABLE=1` alone had proved ineffective locally and is not the evidence for uncached compilation. The actual compiler debug format stayed `/Z7`; debug compile flags included `/Ob0 /Od /RTC1`, and linker flags included `/debug /INCREMENTAL`. Cold removal of EXE/PDB/ILK prevents reuse of prior incremental-link artifacts.

These are two observations per cohort on one machine. The baseline times ranged from 200.063 to 210.905 seconds; temperature, filesystem caching and scheduler variation remain possible influences. The results support a local build-time/OBJ/PDB improvement under these settings, not a universal performance guarantee. Test runtime and rendering correctness were not measured by this build experiment. A separate [selected-task runtime report](integration-runtime-measurement.md) records eight actual CTest rounds; its scope and samples are distinct. Optional profile build cost was outside the timed target scope.

All earlier split/Unity and direct-43-source partial or failed attempts are excluded. Only the completed v3 ABBA experiment supplies the numerical claims above. Raw logs, generated build metadata and input archives remain ignored local evidence because they contain machine paths; they are not public artifacts.

## Final workspace restoration

Final sources and build inputs were restored and verified: all 387 frozen-input hashes matched. The standard `make build/win32-debug JOBS=4` completed successfully after the benchmark, linking the engine DLLs, all 25 test executables and native plugins (2,506 build steps). Compiled discovery indexed 7,275 cases; this is not a claim that the full suite ran. All 22 focused CTest registrations passed against the restored SHARED artifacts in 2.91 seconds. The full OBJECT build and the same 22 focused registrations also passed (2.86 seconds). These restoration checks are outside the timed ABBA intervals.
