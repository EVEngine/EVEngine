# ios target SDK notes

This tree is the iOS packaging template inside `dist/eve-sdk/ios[-debug]`.

Use a macOS host SDK of the same version and Xcode command line tools (including
Python 3). Script-only games reuse the prebuilt device `.app`; no engine compile
or Vulkan SDK install is needed. MoltenVK is already linked/embedded by the engine
build; consumers must not inject a second implementation.

```sh
/path/to/macosx-sdk/bin/eve build ios ./game --sdk /path/to/ios-sdk \
  -o ../ios-output --name MyGame --bundle-id com.example.mygame \
  --sign-identity '<certificate SHA-1 or exact name>' \
  --profile /path/to/development.mobileprovision
```

The output contains a development-signed `.app` and `.ipa`. The certificate/private
key must be in an unlocked keychain; the profile must be unexpired and authorize
that identity and bundle ID. Add `--device <provisioned-device-UDID>` to install and
launch on a trusted, unlocked device with Developer Mode enabled. Output must be a
new directory outside the game and SDK. Failed signing does not publish an app.

For assembly without a certificate, replace signing options with `--unsigned`.
This output cannot run on a physical device and produces no signed IPA. For a
simulator-built SDK use `--simulator`; the published device SDK cannot run on a
simulator. Development signing is currently the supported device path; App Store
and distribution exports require a separate workflow.

Formal release CI requires the existing `IOS_CERTIFICATE_P12_BASE64`,
`IOS_CERTIFICATE_PASSWORD`, `IOS_DEVELOPMENT_TEAM` and
`IOS_PROVISIONING_PROFILE_BASE64` secrets, containing a development identity and
profile. It signs a consumer game/IPA rather than trusting the shell's signature.
Physical-device acceptance is available through the manual `Apple SDK device
acceptance` workflow: configure a dedicated self-hosted `ios-device` Mac runner,
connect the provisioned device and set `IOS_TEST_DEVICE_UDID`. The workflow checks
startup output and continued process liveness. Simulator CI remains independent.

## iOS test app (zeroerr suite)

The unit-test suite can be packaged as its own iOS app, mirroring the Android
test app (`EVTestActivity`): the `eve` executable is replaced by the zeroerr
runner, and `test/` + `examples/` are bundled into the app and staged into a
writable directory on first launch (the bundle is read-only on iOS).

On a **macOS dev machine with Xcode** and a connected, unlocked device:

```sh
make build/ios-debug-test            # configure + deps + tests + signed eve.app
make run/ios-test-debug              # install + launch the full suite
make run/ios-test-debug FILTER=math.*   # only tests matching the filter
make log/ios-test                    # stream zeroerr results (Ctrl-C to stop)
```

`run/ios-test-debug` launches the app with `devicectl --console`, so zeroerr
results stream straight into the terminal (device logs are not forwarded to the
host unified log on all setups). The test runner exits after the suite
finishes, so the terminal returns once the PASSED/FAILED summary appears.

For the full suite, run one test file per app launch instead of everything in
a single process — rapid window create/destroy across hundreds of tests trips
the iOS watchdog and kills the app (SIGKILL), which is why CI also isolates
each case into its own process:

```sh
make run/ios-test-all-debug          # loops test/*.cpp, logs to build/ios-test-results/
```

The runner now supports `-evengine.test.file <basename>.cpp` (per-file filter)
and exits after the suite completes, so the loop above is fully scriptable.
The first launch stages the bundled `test/` + `examples/` trees; a marker file
makes later launches skip that ~26 MB copy, and the target prints a
PASS/FAIL/CRASH summary per test file at the end. Note: on real devices the
watchdog kills apps that create/destroy many windows in one process (SIGKILL),
and asset-dependent files such as ClassicScenes can segfault without the
downloaded scenes — per-file isolation contains those to the affected file.

The test app installs side-by-side with the game shell under a separate bundle
identifier (`com.evengine.example.test`, override with `IOS_TEST_BUNDLE_ID`).
The filter is passed as a launch argument (`-evengine.test.filter math.*`,
mirroring Android's `evengine.test.filter` intent extra) or falls back to
`--testcase=<pattern>` on the process argument list. Without a filter the full
suite runs and results appear in `make log/ios-test`.

### Simulator runs (no signing / no device needed)

```sh
make build/ios-sim-debug-test        # separate ios-simulator-debug deps + app
make run/ios-sim-test-debug [FILTER=math.*]
make log/ios-sim-test
```

The simulator build ad-hoc signs the app and the embedded MoltenVK framework
(no Apple Development identity or unlocked keychain required), and keeps its
own third-party tree so the device deps are not clobbered.
`run/ios-sim-test-debug` boots the first available simulator, installs the app
and launches it with the filter (if any). Test output is written to unified
logging (os_log) and stderr, so it shows up both in `log stream`/`log show`
and in `simctl launch --console-pty`; the app stays resident after the suite
finishes (normal iOS lifecycle), so Ctrl-C the log command when the summary
appears.
