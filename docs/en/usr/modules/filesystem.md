# Files and Hot Reload

[中文](../../../usr/modules/filesystem.md) · [Module handbook](../MODULES.md)

`eve.Filesystem()` handles virtual file access; `eve.HotReload()` watches and replaces resources. Startup creates the common instances `fs` and `hot`.

```squirrel
local fs = eve.Filesystem();
local data = fs.read("save/profile.json");
fs.write("save/backup.json", data);
fs.watch("config/game.json");
```

Check each operation's documented failure value; these fragments assume existing files and a configured writable directory. Game paths use `/` and are relative virtual paths. Writes must stay inside the configured write/save directory. Packaged resources should be read through the virtual filesystem rather than absolute OS paths.

## Player saves

Use `writeTextAtomic(relativePath, text)` for important saves. Its script binding returns `1` on success and `0` on failure, rather than the native Result. Native platforms write and flush a same-directory temporary file before replacing the destination. A write-stage failure keeps the previous file. The path must remain inside the configured save directory and cannot traverse parents.

WebGPU currently cannot offer the same atomic replacement guarantee and explicitly reports failure. `writeText()` remains appropriate for rebuildable output or disposable caches. Always check success and keep a backup policy. See [saving games](../SAVE.md) for authoritative gameplay state and restore validation.

## Watch configuration changes

Register `fs.watch(path)`, poll `pollWatch()` during update, and use `getLastWatchPath()` to reload changed configuration. Create a replacement resource first; publish it only after a successful load, leaving the existing resource intact on failure.

`hot.watchTree(root)` recursively watches content. If a directory is created during runtime, `hot.watchNewDirectory(path)` registers it and its current descendants. Roots may be virtual paths, `.`, relative paths containing `..`, or absolute OS directories. Parent-relative roots resolve against the real working directory rather than PhysFS.

`config.hotReloadWatch` chooses startup watch roots; it accepts a string or string array and defaults to `["."]`. The normal runtime owns its reload loop; custom watchers must still be polled.

## Remote hot reload

APK and iOS bundle resources are read-only. Edit on a development machine and synchronize into a writable overlay:

```sh
eve dev --port 8765 /path/to/game
```

On the device:

```squirrel
config.devServer = "http://192.168.1.5:8765"
config.devSyncMs = 1000 // Poll interval in milliseconds.
```

You can also inject the server URL using `eve run --dev-server http://192.168.1.5:8765`. The client compares the server manifest's modification times/sizes, downloads changed files to a writable directory, mounts it with highest priority and uses the existing script/resource reload pipeline. Script and asset changes do not require reinstalling the app.

## Prepared immutable files

`fs.requestPreparedFile(path)` submits CPU reading and returns a structured Result. It needs initialized filesystem and thread resource services. Duplicate requests share a ResourceManager cache key. Existing immutable FileData snapshots remain valid after cache unload/reload.

The native `readPreparedFile(path, limit)` waits for a prepared task or loads synchronously. The maximum single-file size is 1 GiB, with smaller caller limits enforced separately. Empty paths and the reserved `?` suffix are rejected. Submission/synchronous reads happen on the game thread; workers do not access VM/GPU. Drain tasks before destroying the filesystem.

See [API conventions](../API-CONVENTIONS.md) for return contracts and exact script bindings. [Source](../../../../src/modules/filesystem/).
