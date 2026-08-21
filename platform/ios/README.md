# ios target SDK notes

This tree is the iOS packaging template inside `dist/eve-sdk/ios[-debug]`.

On a **macOS dev machine** with Xcode:

1. Use the prebuilt `bin/eve.app` (or rebuild packaging around the SDK host) as the app shell.
2. Place game assets under the app's `game/` resources (see `game-shell/`).
3. Ensure MoltenVK is embedded as in the engine build.
4. Sign and archive for device / App Store.

Native plugins are typically embeddable `.dylib` / frameworks loaded via `eve.Plugins().load(...)`.

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

The test app installs side-by-side with the game shell under a separate bundle
identifier (`com.evengine.example.test`, override with `IOS_TEST_BUNDLE_ID`).
The filter is passed as a launch argument (`-evengine.test.filter math.*`,
mirroring Android's `evengine.test.filter` intent extra) or falls back to
`--testcase=<pattern>` on the process argument list. Without a filter the full
suite runs and results appear in `make log/ios-test`.
