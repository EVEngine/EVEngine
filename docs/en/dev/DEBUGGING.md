# Debugging Engine Changes

Source: [repository debugging playbook](../../../AGENTS.md#debugging-playbook). [Testing](TESTING.md) · [Developer index](README.md).

Start with the exact failing executable, case and input. Distinguish a source defect from a missing SDK, stale artifact, timeout or launch failure. Read the API contract before attributing a failure to a GPU/driver.

## Vulkan validation and device selection

Set `EVENGINE_VULKAN_VALIDATION=1` before instance creation to enable Khronos validation. VUID messages identify the violated rule. Validation substantially slows a run; use a short reproduction.

Use `EVENGINE_GPU_DEVICE=integrated`, `discrete` or `first` to compare available device paths. Confirm feature bits and validation output before introducing vendor-specific code.

Reduce the failure to a minimal test. If SDL, dependency initialization or surface creation is involved, directly probe that layer. A working probe beside a failing engine path points to engine state/lifetime handling.

## Windows crash traces

`src/engine/common/CrashHandler.h` provides `eve::installCrashHandler()`, installed in both `src/engine/main.cpp` and `test/main.cpp`. Windows tests link `EVBacktrace`.

For crashes with only an exit code, confirm the handler exists in the actual binary. Search the executable for `[crash] code=`. `EVE_TEST_CRASH=1` forces a startup access violation solely to test handler output.

backward-cpp prints the most recent call last: frame N+1 called N. Exception-dispatch frames around `KiUserExceptionDispatcher` are machinery; the frame directly above it is the crash site. An exception address inside engine code commonly indicates a bad read; a near-zero instruction address can indicate a null indirect call. Verify the particular trace.

## Isolate state and build inputs

A commit in Git does not prove the launched binary includes it. Check the artifact and launch path. Use MSVC `/showIncludes` output to identify the selected header copy. `<SDL2/SDL.h>` does not include `SDL_vulkan.h`; include it explicitly for Vulkan entry points.

The repository's Ninja paths can leave translation units stale after a header edit because of unscanned dependency tracking. Mixing changed class layouts with old objects can produce nonsense values or deterministic crashes. Rebuild all affected translation units before drawing conclusions. If necessary, configure a fresh separate build directory and reuse immutable prebuilt dependencies rather than sharing mutable build trees.

Check dependency library timestamps when upstream behavior changes unexpectedly. If an SDL call works in a probe, instrument subsystem initialization/shutdown pairing, global state and window flags before changing SDL or driver code.

## Runtime and visual evidence

Use `make devlab` for the developer example or launch a game with the debug option. The [user guide](../usr/README.md) covers script debugging and hot reload.

For rendering changes, inspect an engine-produced screenshot from the real path. Compare the same scene under controlled inputs. Compilation, a software-Vulkan launch or an unrelated image is insufficient visual evidence.
