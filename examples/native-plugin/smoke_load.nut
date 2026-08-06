// Quick plugin load smoke (no window — may still init SDL via modules).
// Prefer running under example/ with a one-off script if needed.

function file_exists(path) {
    try { file(path, "r").close(); return true; } catch (e) { return false; }
}

print("plugins smoke\n");
local plugins = eve.Plugins();
local path = "build/hello_plugin.dylib";
try {
    plugins.load(path);
    local hi = eve.HelloPlugin();
    print(hi.greet() + "\n");
    print("OK\n");
} catch (e) {
    print("FAIL: " + e + "\n");
}
