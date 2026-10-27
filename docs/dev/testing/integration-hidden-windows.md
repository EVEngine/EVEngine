# Hidden native Windows tests

Windows Make test entrypoints default to `TEST_WINDOWS=hidden`. The launcher
creates a private desktop and explicitly places CTest there using
`CreateProcessW`'s `STARTUPINFO.lpDesktop`. It never activates that desktop.
SDL windows keep their normal shown state, so Vulkan presentation, pixel
readback and visibility-event contracts continue to execute.

`SDL_HideWindow` is unsuitable here: the engine's event sink intentionally pauses
rendering for hidden windows. Merely hiding a console also does not isolate SDL
windows. A two-generation process probe verifies actual desktop inheritance
before CTest starts. Isolation failure is fatal rather than a visible fallback.
An owned Job Object terminates remaining test descendants when the launcher exits
or is interrupted. Standard streams, environment, working directory, arguments
and the CTest exit code are preserved.

Use `make test/win32-debug TEST_WINDOWS=visible` for interactive diagnosis.
Direct CTest commands need `python3 scripts/run_ctest.py -- ctest ...` to use the
same isolation. Linux and macOS Make entrypoints are unchanged.

## Native verification

The [machine-readable evidence](integration-hidden-windows.json) records:

- Four real cases passed: compute-generated cloth rendering, lighting frame-slot
  reuse, desktop lifecycle events and application background/foreground resume.
  Their actual render/present/readback assertions remained enabled.
- A second run enumerated window ownership over 204 samples. It observed 44
  background test HWNDs, including shown windows, and zero test HWNDs on the input
  desktop. This observation covers these four cases, rather than every case.
- All 93 cases in the four changed integration sources completed: 82 passed and
  the same 11 pre-existing GPU failures remained. No new failure was introduced.
- All five migrated long benchmark cases passed when explicitly built and run.
- Fourteen launcher regression tests passed, including real descendant desktop
  inheritance and termination of a leftover descendant on Job Object close.

The launcher changes Windows process placement only. No production Window API,
render lifecycle, module interface, ECS model or persistent format changed.
Runtime comparisons against earlier visible runs must disclose this changed
condition; these checks do not establish a controlled performance A/B result.
