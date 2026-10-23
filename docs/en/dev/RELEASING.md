# Release Process

[中文](../../dev/发布流程.md) · [Developer index](README.md)

`main` is the latest official release and default branch. Development happens on `dev`; each release uses an isolated `vMAJOR.MINOR.PATCH` branch. `scripts/release.py` implements these conventions.

## Repository setup

Keep `main` as default. Initial setup, if dev does not yet exist:

```sh
git fetch origin
git branch dev origin/main
git push -u origin dev
```

Recommended main protection requires PRs and `main-gate`, blocks force pushes/deletions and has no bypass. The gate allows documentation-only changes or official `promote/vX.X.X` PRs; it rejects direct engine-code promotion from dev.

dev may require PRs. The Chinese policy cautions against mandatory status checks for token-created rebase/sync-docs PRs because they may not trigger workflows. Existing sync-docs automation opens a PR without directly pushing dev; this is separate from hand-maintained English translations.

## Publish a version

1. Ensure the intended commit is on dev.
2. Create a GitHub Pre-release at that dev HEAD with a tag such as `v0.1.0`. Tags must use `vMAJOR.MINOR.PATCH`; the tree initially has the development version.
3. Release runs `release.py start`: create the version branch, remove `-dev`, move the tag to the release commit. Run strict Debug CI, desktop Release builds/tests and mobile Release builds, then package five-platform SDKs and attach them.
4. `release.py finish` promotes the release, restores the same numeric version plus `-dev` on the version branch and opens two PRs: `promote/vX.X.X` to main and `rebase/vX.X.X` to dev. It directly pushes neither protected branch.
5. A maintainer merges promotion first, then rebase.

On rebase conflicts, follow the PR's continuation instructions and push the resolution to its rebase branch; never force-push dev. A new Pre-release tag determines the next number: a higher number than the tree is permitted, downgrading is rejected. finish restores the released number plus `-dev`, not an invented next version.

## Version consistency

Required touchpoints include `CMakeLists.txt`, Android template versionName/derived versionCode, MCP serverInfo.version, version banners in basic/iOS/Android game-shell scripts and Doxygen's version in `docs/CMakeLists.txt`.

start rewrites required touchpoints together. `python3 scripts/release.py check-versions` catches mismatches. Independent tool package versions report warnings and are not rewritten as engine versions. Android's code formula is `major*10000 + minor*100 + patch`.

## SDK packaging

SDKs preserve engine/vendored licenses and relative attribution paths under `share/eve/licenses/`. Enabled module headers follow the module manifest rather than a separate whitelist.

Five-platform SDK tests check layout, exact versions, zip/package operations and native plugin compilation via `find_package(EVEngine)` / `add_eve_plugin()`. Android also assembles the template APK; iOS checks the device bundle, Mach-O executable and arm64 slice. Missing required mobile tools fail rather than skip.

Before publication, every archive's `share/eve/VERSION` must match the tag. Upload SHA256SUMS with SDKs; artifact attestations may be enabled.

## Consumer validation

After upload, sdk-release.yml exercises real artifacts through consumer-test.sh:

- Desktop hosts download their SDK, verify eve's version, fetch the official Android SDK/toolchain with checksum validation, build/inspect an APK and its runtime/game assets/launcher, package a host game and actually launch it. Require `EVE_CI_GAME_OK`.
- Supported cross-host packaging combinations produce Windows/Linux games run on their target platforms with software Vulkan. Packaging alone is insufficient.
- Android collects APKs from all three desktop host consumers, installs each in an arm64 emulator, launches `EVEngineActivity` and requires the logcat startup marker. Missing artifacts or failed launches block publication.
- iOS builds a simulator application from release source, installs/launches with simctl and checks the marker. Device SDK bundle/arm64 checks remain separate because device apps cannot run in the simulator.

Windows SDKs bundle the Vulkan loader and VC runtime. Packaging selects runtime names using TARGET_PLATFORM. Unbuffered stdout makes startup markers visible to CI promptly.

## Retry and cleanup

start is idempotent for the same tag. A strict-test/SDK failure leaves the release commit/tag and Pre-release intact: repair and rerun Release for that tag. A partly completed finish reuses existing official release and promotion/rebase PRs. Resolve conflicts on the rebase branch.

cleanup-branches removes merged version/promotion/rebase branches through the scheduled or manually dispatched workflow. It never deletes tags, main or dev.

## Documentation-only main changes

main-gate permits README variants, docs/** and Doxyfile. After merging, existing Sync docs opens sync-docs/from-main to dev using only document-path differences since the merge base. Other changes belong on dev; release touchpoints follow the release rebase pipeline.

## Local commands

The script requires GitHub CLI on PATH. Preview before write operations:

```sh
python scripts/release.py --dry-run start --tag v0.1.0
python scripts/release.py start --tag v0.1.0
python scripts/release.py finish --tag v0.1.0
python scripts/release.py sync-docs
python scripts/release.py check-main-pr
python scripts/release.py check-versions
python scripts/release.py cleanup-branches
```

The version is illustrative, not an instruction to create that release. Script tests run without network:

```sh
python -m unittest discover -s scripts/tests -p "test_*.py" -v
```
