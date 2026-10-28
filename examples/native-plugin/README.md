# Native plugin example (no engine source required)

Build against an installed **target-platform** SDK:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DEVEngine_DIR=$PWD/../../dist/eve-sdk/macosx-debug/cmake
cmake --build build
```

Match the SDK's build configuration: use `Debug` for a debug SDK and `Release`
for a release SDK. On Windows this also matches the host's CRT and STL ABI;
mixing them can crash even when the plugin links successfully. From the repository
root, a Windows debug SDK build is:

```powershell
cmake/with-msvc.cmd cmake.exe -S examples/native-plugin -B build/native-plugin -G Ninja -DCMAKE_BUILD_TYPE=Debug -DEVEngine_DIR=$PWD/dist/eve-sdk/win32-debug/cmake
cmake/with-msvc.cmd cmake.exe --build build/native-plugin
```

Then from a game directory (with the matching `eve` host):

```squirrel
local plugins = eve.Plugins();
plugins.load("./hello_plugin.dylib"); // .so on Linux/Android, .dll on Windows
local hi = eve.HelloPlugin();
print(hi.greet() + "\n");
```

After rebuilding the library at the same path, reload it without restarting:

```squirrel
local result = plugins.reload("./hello_plugin.dylib");
if (result.ok) {
    hi = eve.HelloPlugin();
    print(hi.greet() + "\n");
} else {
    print(result.status.summary + "\n");
}
```

Use the current SDK for every generation. `reload` publishes new classes and
fresh module instances; previously captured classes, objects and methods keep
their original generation. It does not migrate native state or automatically
watch/recompile files. A failed initialization or binding leaves the old version
active. Publish the completed build before calling `reload`, on the main thread.

The library directory must be writable for unique sibling copies. Old code and
module instances stay resident until process exit for closure safety, so memory
usage grows with reloads. Windows also retains `.eve-<pid>-<serial>-<library>`
copies until the host exits; they can then be deleted. See the
[plugin lifecycle contract](../../docs/usr/modules/plugins.md) for state,
cross-generation objects, external services and binding restrictions.
