# Fuzz testing

EVEngine uses zeroerr's structured fuzz API with libFuzzer. Fuzz harnesses are
small standalone Clang builds under `test/fuzz`; the normal Linux engine build
continues to use GCC.

## Run locally

Install Clang (including compiler-rt), CMake and Ninja, initialize submodules,
then run:

```sh
make fuzz/linux
```

The default budget is 2,000 executions per harness. Override the deterministic
budget and seed when reproducing or running a longer campaign:

```sh
EVENGINE_FUZZ_RUNS=50000 EVENGINE_FUZZ_SEED=123 make fuzz/linux
```

Harnesses run with AddressSanitizer and UndefinedBehaviorSanitizer. CI uses a
short budget for pull requests and a longer budget on the weekly schedule. If
libFuzzer finds a failure, CI uploads its `crash-*`, `leak-*`, `oom-*` or
`timeout-*` reproducer.

## Adding a harness

- Put it in `test/fuzz` and use `FUZZ_TEST_CASE` / `FUZZ_FUNC` from zeroerr.
- Prefer structured domains and include known boundary inputs with `WithSeeds`.
- Keep the target small: compile only the parser or subsystem boundary under
  test instead of linking the complete engine.
- Make every run deterministic through `EVENGINE_FUZZ_SEED`.
- When a defect is found, commit a normal regression test before fixing it;
  keep compact inputs as permanent seeds when they improve future exploration.
