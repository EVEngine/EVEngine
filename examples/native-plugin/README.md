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
