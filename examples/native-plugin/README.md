# Native plugin example (no engine source required)

Build against an installed **target-platform** SDK:

```bash
cmake -B build -DEVEngine_DIR=$PWD/../../dist/eve-sdk/macosx-debug/cmake
cmake --build build
```

Then from a game directory (with the matching `eve` host):

```squirrel
local plugins = eve.Plugins();
plugins.load("./hello_plugin.dylib"); // .so on Linux/Android, .dll on Windows
local hi = eve.HelloPlugin();
print(hi.greet() + "\n");
```
