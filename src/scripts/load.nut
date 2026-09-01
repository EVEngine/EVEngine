// Engine root script.
//
// Which modules exist depends on how the engine was built (see
// cmake/module_manifest.cmake), so nothing here names a module that might have
// been trimmed. `eve.moduleList` is generated at configure time from the same
// manifest that drives the link list. `config.modules` / `optionalModules`
// select which of those slots are constructed; omitted fields keep the old
// "instantiate everything in the build" behaviour. Optional modules are used
// behind `has_module()` / `ensure_module()` guards. Native class methods bind
// on first `eve.ClassName` access; `m.cls in eve` does not trigger that.
//
// The frame body lives in eve_frame() so both drivers can share it: desktop
// runs the while loop at the bottom, while the browser build returns to C++
// and is driven from emscripten_set_main_loop instead.

function file_exists(path) {
    try {
        file(path, "r").close();
        return true;
    } catch (e) {
        return false;
    }
}

function path_endswith(str, suffix) {
    if (str == null || suffix == null) return false;
    if (str.len() < suffix.len()) return false;
    return str.slice(str.len() - suffix.len()) == suffix;
}

function normalize_path(path) {
    if (path == null) return "";
    local s = path;
    while (s.len() >= 2 && s.slice(0, 2) == "./")
        s = s.slice(2);
    return s;
}

/** True when the build contains this module and it has been instantiated. */
function has_module(slot) {
    return slot in getroottable() && getroottable()[slot] != null;
}

function _module_entry(slot) {
    if (!("moduleList" in eve)) return null;
    foreach (m in eve.moduleList) {
        if (m.slot == slot) return m;
    }
    return null;
}

/**
 * Instantiate a script module by root slot if the build contains it.
 * Safe to call more than once. Returns true when the slot is live.
 */
function ensure_module(slot) {
    if (has_module(slot)) return true;
    local m = _module_entry(slot);
    if (m == null) return false;
    local _m0 = clock();
    try {
        getroottable()[slot] <- eve[m.cls]();
    } catch (e) {
        print("module " + m.cls + " failed to initialize: " + e + "\n");
        return false;
    }
    local _mdt = (clock() - _m0) * 1000.0;
    if (_mdt >= 50.0)
        print("[startup] module " + m.cls + " took " + _mdt + " ms\n");
    return has_module(slot);
}

// Frame loop / window / reload always need these slots when the build has them.
_boot_module_slots <- ["fs", "hot", "timer", "platform_event", "win", "gfx"];

function _mark_slots(dest, arr) {
    if (arr == null) return;
    foreach (slot in arr)
        dest[slot] <- true;
}

function instantiate_configured_modules() {
    local filtering = ("modules" in config) || ("optionalModules" in config);
    local wanted = {};
    if (filtering) {
        _mark_slots(wanted, _boot_module_slots);
        if ("modules" in config) {
            if (typeof config.modules != "array")
                throw "config.modules must be an array of module slot strings";
            _mark_slots(wanted, config.modules);
        }
        if ("optionalModules" in config) {
            if (typeof config.optionalModules != "array")
                throw "config.optionalModules must be an array of module slot strings";
            _mark_slots(wanted, config.optionalModules);
        }
    } else {
        // Compatibility mode historically exposed every native class before
        // constructing modules. Preserve that shared-type registration order.
        if (eve._bindAllNativeClasses() < 0)
            throw "failed to bind native module classes";
    }
    local n = 0;
    foreach (m in eve.moduleList) {
        if (filtering && !(m.slot in wanted)) continue;
        if (ensure_module(m.slot))
            n += 1;
    }
    _startup_ms("all modules instantiated");
    _log("boot: " + n + " module(s) instantiated" + (filtering ? " (filtered)" : ""));
}

// ---------------------------------------------------------------------------
// 通用游戏开发辅助（在 main.nut 之前定义；示例脚本可直接使用，
// 游戏脚本里可以按需用同名定义覆盖）。
// ---------------------------------------------------------------------------

_input_edge_state <- { };

// 边沿检测：只在“刚按下”的那一帧返回 true。用于按键（可带备用键名）。
// 状态跨热重载保留，所以改脚本保存后不会丢按键边沿。
function key_just_pressed(name, alternate = "") {
    if (!has_module("keyboard")) return false;
    local down = keyboard.isDown(name) ||
                 (alternate != "" && keyboard.isDown(alternate));
    local key = (alternate == "") ? ("k_" + name) : (name + ":" + alternate);
    local was = (key in _input_edge_state) ? _input_edge_state[key] : false;
    _input_edge_state[key] <- down;
    return down && !was;
}

// 数值裁剪。
function clampf(v, lo, hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// 热重载持久化：name 已存在于根表则直接返回，否则调用 init() 创建并保存。
// 约定：`x <- persist("x", function() { return <初始值>; });`
function persist(name, init) {
    local root = getroottable();
    if (!(name in root))
        root[name] <- init();
    return root[name];
}

config <- {
    width = 800
    height = 600
    title = "EVEngine"
    debug = false
    hotReload = false
    // Remote hot reload: set to the `eve dev` URL of the host machine, e.g.
    // "http://192.168.1.5:8765". Scripts/resources changed there are synced and
    // reloaded live on this device (used on iOS/Android where the bundle is read-only).
    devServer = ""
    devSyncMs = 1000
};

if (file_exists("config.nut")) {
    dofile("config.nut");
}
if (!("hotReload" in config)) {
    // Off in the browser: the preloaded VFS has no file watcher to poll.
    config.hotReload <- !("hostDrivesFrames" in eve && eve.hostDrivesFrames);
}

// `eve run --dev-server <url>` overrides config.devServer (useful on mobile
// builds where config.nut is baked into a read-only bundle).
if ("devServerArg" in eve && eve.devServerArg != null && eve.devServerArg != "")
    config.devServer <- eve.devServerArg;

// ---------------------------------------------------------------------------
// Startup timing (temporary diagnostics; remove once startup is fast).
// ---------------------------------------------------------------------------
_startup_t0 <- clock();
print("[startup] load.nut in scripts begins at process clock " + (clock() * 1000.0) + " ms\n");

function _startup_ms(label) {
    local ms = (clock() - _startup_t0) * 1000.0;
    print("[startup] " + label + ": " + ms + " ms\n");
}

// Append a milestone to the crash/error log (eve.log) so a crash shows how far
// the boot sequence got. Bound by the host (eve.log); no-op when unavailable.
function _log(msg) {
    if ("log" in eve) eve.log("info", msg);
}

// ---------------------------------------------------------------------------
// Bind the modules this build actually contains.
// When config.modules / optionalModules is set, only those slots plus the
// boot set (fs/hot/timer/platform_event/win/gfx) are constructed.
// ---------------------------------------------------------------------------

instantiate_configured_modules();

// Project-wide module requirements belong to config.nut.  Validate them only
// after the build's module list has been instantiated, and before any game
// entry point can observe a partially configured runtime.
function validate_project_modules() {
    if ("modules" in config) {
        if (typeof config.modules != "array")
            throw "config.modules must be an array of module slot strings";
        foreach (slot in config.modules) {
            if (typeof slot != "string")
                throw "config.modules entries must be module slot strings";
            if (!has_module(slot))
                throw "required module is missing: " + slot;
        }
    }
    if ("optionalModules" in config) {
        if (typeof config.optionalModules != "array")
            throw "config.optionalModules must be an array of module slot strings";
        foreach (slot in config.optionalModules) {
            if (typeof slot != "string")
                throw "config.optionalModules entries must be module slot strings";
        }
    }
}

validate_project_modules();

if (!has_module("win") || !has_module("gfx")) {
    print("engine build is missing the window or graphics module\n");
    return;
}
local s = eve.WindowSettings();
s.width = config.width;
s.height = config.height;
s.centered = true;
if (!win.setWindowSettings(s)) {
    print("setWindowSettings failed\n");
    return;
}

// Keep script layout in sync with the real window (mobile may ignore 800x600).
config.width = win.getWidth();
config.height = win.getHeight();
_startup_ms("window created + vulkan initialized");
_log("boot: window + vulkan initialized (" + config.width + "x" + config.height + ")");

// Node-style async (Promise / nextTick / setTimeout). Embedded via eve.asyncScript.
if ("asyncScript" in eve && eve.asyncScript != null && eve.asyncScript != "") {
    try {
        compilestring(eve.asyncScript)();
    } catch (e) {
        // Report FIRST: with "break on error" the debugger pauses at the
        // throwing script line / catch site before stdout is flushed.
        if ("dev" in eve) eve.dev.reportError("" + e);
        print("async runtime failed to load: " + e + "\n");
    }
} else if (file_exists("async.nut")) {
    try {
        dofile("async.nut");
    } catch (e) {
        if ("dev" in eve) eve.dev.reportError("" + e);
        print("async.nut failed to load: " + e + "\n");
    }
}
_startup_ms("async runtime loaded");

eve_init <- function() {};
eve_update <- function(dt) {
    // Keep scene node world transforms fresh for games that mutate nodes from
    // scripts. Clean trees skip the pass entirely (incremental transform).
    if (has_module("scene")) scene.updateTransformsAll();
};
eve_render <- function() {
    gfx.clear();
};
eve_quit <- function() {};

// ---------------------------------------------------------------------------
// Soft hot-reload bookkeeping (disk main.nut only; embedded demo is build-time).
// ---------------------------------------------------------------------------

watched_scripts <- [];

function track_script(path) {
    local p = normalize_path(path);
    if (p == "" || !path_endswith(p, ".nut")) return;
    foreach (existing in watched_scripts) {
        if (existing == p) return;
    }
    watched_scripts.append(p);
}

// ---------------------------------------------------------------------------
// State hot reload helpers (capture -> reload -> restore).
// Old state is authoritative (captured by beginStateReload); fields the new
// script adds are kept, and scripts may rebuild class instances in
// eve_after_reload() using migrate_instance / remap_instances.
// ---------------------------------------------------------------------------

// Copy the enumerable fields of an old instance/table into a fresh instance
// of NewClass; fields missing on the new class keep their defaults.
function migrate_instance(old, NewClass) {
    local n = NewClass();
    if (old == null) return n;
    foreach (k, v in old) {
        try {
            n[k] <- v;
        } catch (e) {
            // Not assignable on the new class; keep the default.
        }
    }
    return n;
}

// Replace every element/field of an array/table with a migrated instance.
function remap_instances(container, NewClass) {
    if (typeof container == "array") {
        for (local i = 0; i < container.len(); ++i) {
            container[i] = migrate_instance(container[i], NewClass);
        }
    } else if (typeof container == "table") {
        local keys = [];
        foreach (k, v in container) keys.append(k);
        foreach (k in keys) {
            container[k] = migrate_instance(container[k], NewClass);
        }
    }
    return container;
}

// Compile every candidate before touching the live root table.  loadfile()
// returns a closure without executing it, so a syntax error cannot leave a
// partially redefined game behind.
function compile_reload_candidates() {
    local candidates = [];
    foreach (p in watched_scripts) {
        if (!file_exists(p)) continue;
        candidates.append({ path = p, closure = loadfile(p) });
    }
    return candidates;
}

// Keep a shallow snapshot of every root binding, including functions, classes
// and native objects that StateValue deliberately cannot serialize.  State
// roots are restored separately by ReloadSession; this snapshot restores the
// definition/binding surface and removes slots introduced by a failed script.
function capture_reload_bindings() {
    local bindings = {};
    foreach (k, v in getroottable())
        bindings[k] <- v;
    return bindings;
}

function restore_reload_bindings(bindings) {
    local root = getroottable();
    local current = [];
    foreach (k, v in root) current.append(k);
    foreach (k in current) {
        if (!(k in bindings)) delete root[k];
    }
    foreach (k, v in bindings) {
        if (k in root)
            root[k] = v;
        else
            root[k] <- v;
    }
}

function report_reload_failure(message) {
    if ("dev" in eve) eve.dev.reportError(message);
    print(message + "\n");
}

// Transient roots deliberately start from the new script's defaults. Delete
// their old bindings only after ReloadSession has captured the rollback point.
function reset_transient_state() {
    if (!("dev" in eve) || !("transientStateRoots" in eve.dev)) return;
    local root = getroottable();
    foreach (name in eve.dev.transientStateRoots()) {
        if (name in root) delete root[name];
    }
    // Candidate scripts declare the complete next policy set. This makes
    // deleting an obsolete declaration effective; abort restores the old set.
    eve.dev.clearStateRoots();
}

function soft_reload_scripts() {
    // Stage ①: compile the complete candidate set before cancelling work or
    // invoking lifecycle hooks.  A broken edit leaves the running game alone.
    local candidates = null;
    try {
        candidates = compile_reload_candidates();
    } catch (e) {
        report_reload_failure("hot-reload compile failed: " + e);
        return false;
    }

    local oldBindings = capture_reload_bindings();
    if ("async_cancel_continuations" in getroottable())
        async_cancel_continuations("soft reload");
    // Stage ②: optional script hook, then capture serializable/native state.
    if ("eve_before_reload" in getroottable()) {
        try {
            eve_before_reload();
        } catch (e) {
            restore_reload_bindings(oldBindings);
            report_reload_failure("eve_before_reload failed: " + e);
            return false;
        }
    }
    local hasSession = ("dev" in eve) && ("beginStateReload" in eve.dev);
    if (hasSession) {
        local e = eve.dev.beginStateReload();
        if (e != "") {
            restore_reload_bindings(oldBindings);
            report_reload_failure("state reload: capture failed: " + e);
            return false;
        }
    }
    reset_transient_state();

    // Stage ③: execute the already-compiled candidates.  Any runtime failure
    // restores the complete old binding surface and captured mutable state.
    foreach (candidate in candidates) {
        try {
            candidate.closure.call(getroottable());
            print("hot-reload script: " + candidate.path + "\n");
        } catch (e) {
            restore_reload_bindings(oldBindings);
            local rollbackError = "";
            if (hasSession) rollbackError = eve.dev.abortStateReload();
            local message = "hot-reload script failed: " + candidate.path + ": " + e;
            if (rollbackError != "") message += "; rollback failed: " + rollbackError;
            report_reload_failure(message);
            return false;
        }
    }

    // Stage ④: captured values win, newly added fields are kept; native
    //    providers are restored / reset by the session.
    if (hasSession) {
        local e = eve.dev.commitStateReload();
        if (e != "") {
            restore_reload_bindings(oldBindings);
            local rollbackError = eve.dev.abortStateReload();
            local message = "state reload: restore failed: " + e;
            if (rollbackError != "") message += "; rollback failed: " + rollbackError;
            report_reload_failure(message);
            return false;
        }
    }
    // Stage ⑤: post-commit hooks rebuild instances derived from restored data.
    if ("eve_after_reload" in getroottable()) {
        try {
            eve_after_reload();
        } catch (e) {
            if ("dev" in eve) eve.dev.reportError("" + e);
            print("eve_after_reload failed: " + e + "\n");
        }
    }
    if ("eve_reload" in getroottable()) {
        try {
            eve_reload();
        } catch (e) {
            if ("dev" in eve) eve.dev.reportError("" + e);
            print("eve_reload failed: " + e + "\n");
        }
    }
    return true;
}

// Apply a single changed path: scripts are re-dofile'd, assets go through the
// native reload registry (particles / tilemaps / textures) plus the optional
// eve_asset_reload hook. Shared by local watch and remote sync.
function handle_change(p) {
    if (path_endswith(p, ".nut")) {
        local isModule = false;
        try {
            if ("reloadScriptModule" in eve) isModule = eve.reloadScriptModule(p);
        } catch (e) {
            report_reload_failure("module hot-reload failed: " + p + ": " + e);
            return;
        }
        if (!isModule) track_script(p);
        soft_reload_scripts();
        return;
    }
    if (has_module("hot")) {
        try {
            hot.tryReload(p);
        } catch (e) {
            if ("dev" in eve) eve.dev.reportError("" + e);
            print("hot-reload asset failed: " + p + ": " + e + "\n");
        }
    }
    if ("eve_asset_reload" in getroottable()) {
        try {
            eve_asset_reload(p);
        } catch (e) {
            if ("dev" in eve) eve.dev.reportError("" + e);
            print("eve_asset_reload failed: " + p + ": " + e + "\n");
        }
    }
}

function poll_hot_reload() {
    if (!config.hotReload) return;
    if (!has_module("fs")) return;
    local scripts = [];
    local assets = [];
    while (true) {
        local kind = fs.pollWatch();
        if (kind == "") break;
        if (kind != "modified" && kind != "added" && kind != "movedTo") continue;
        local p = normalize_path(fs.getLastWatchPath());
        if (p == "") continue;
        if ((kind == "added" || kind == "movedTo") && has_module("hot")) {
            try {
                if (hot.watchNewDirectory(p)) continue;
            } catch (e) {
                if ("dev" in eve) eve.dev.reportError("" + e);
                print("hot-reload directory watch failed: " + p + ": " + e + "\n");
            }
        }
        if (path_endswith(p, ".nut")) {
            scripts.append(p);
        } else {
            assets.append(p);
        }
    }
    if (scripts.len() > 0) {
        foreach (p in scripts) {
            local isModule = false;
            try {
                if ("reloadScriptModule" in eve) isModule = eve.reloadScriptModule(p);
            } catch (e) {
                report_reload_failure("module hot-reload failed: " + p + ": " + e);
                return;
            }
            if (!isModule) track_script(p);
        }
        soft_reload_scripts();
    }
    foreach (p in assets) {
        handle_change(p);
    }
}

// Remote hot reload: pull changed files from the `eve dev` server on the host
// and feed them through the same reload path as the local watchers.
remote_sync_started <- false;

function poll_remote_reload() {
    if (!has_module("hot")) return;
    if (!("devServer" in config)) return;
    local url = config.devServer;
    if (url == null || url == "") return;
    if (!remote_sync_started) {
        local ms = ("devSyncMs" in config) ? config.devSyncMs : 1000;
        if (ms == null) ms = 1000;
        if (hot.startRemoteSync(url, ms)) {
            remote_sync_started = true;
            print("remote hot-reload: " + url + "\n");
        } else {
            print("remote hot-reload failed to start: " + url + "\n");
            return;
        }
    }
    while (true) {
        local p = hot.pollRemoteChange();
        if (p == "") break;
        handle_change(p);
    }
}

if (file_exists("main.nut")) {
    dofile("main.nut");
    track_script("main.nut");
} else if ("demoScript" in eve && eve.demoScript != null && eve.demoScript != "") {
    // Prefer try/catch over `in` — class slot checks differ across SSQ builds.
    try {
        compilestring(eve.demoScript)();
    } catch (e) {
        if ("dev" in eve) eve.dev.reportError("" + e);
        print("Embedded demo failed to load: " + e + "\n");
    }
}
_startup_ms("game script loaded");
_log("boot: game script loaded");

if (config.hotReload && has_module("fs") && has_module("hot")) {
    try {
        // Ensure VFS source is set when Run did not mount (e.g. custom root).
        try { fs.setSource("."); } catch (e) {}
        local n = hot.watchTree(".");
        // Explicit file watch as a second registration (same OS dir, basename filter).
        if (file_exists("main.nut"))
            fs.watch("main.nut");
        print("hot-reload: watching " + n + " path(s)\n");
    } catch (e) {
        if ("dev" in eve) eve.dev.reportError("" + e);
        print("hot-reload watchTree failed: " + e + "\n");
    }
}
_startup_ms("hot reload watch registered");

_startup_ms("eve_init start");
try {
    eve_init();
} catch (e) {
    if ("dev" in eve) eve.dev.reportError("" + e);
    print("eve_init failed: " + e + "\n");
}
_startup_ms("eve_init done");

// ---------------------------------------------------------------------------
// DevTools helpers (only present when `eve run --debug`).
// ---------------------------------------------------------------------------

function has_dev() {
    return ("dev" in eve);
}

function dev_poll() {
    if (has_dev())
        eve.dev.poll();
}

function dev_should_update() {
    if (!has_dev()) return true;
    return eve.dev.shouldRunUpdate();
}

function dev_notify_frame_done() {
    if (has_dev())
        eve.dev.notifyFrameDone();
}

// Scenario recording: mark a new frame bucket before this frame's events are
// polled, so the recorded input/event stream maps to the frame that consumed it.
function dev_scenario_frame() {
    if (has_dev())
        eve.dev.scenarioFrame();
}

function handle_dev_key(key, scancode) {
    if (!has_dev()) return;
    // Pause key (keyboard Pause/Break) toggles frame-level pause.
    if (key == "Pause" || scancode == "Pause") {
        eve.dev.togglePause();
        print(eve.dev.isPaused() ? "dev: paused\n" : "dev: resumed\n");
        return;
    }
    // F5 = continue to next breakpoint (continueRun; `resume` is a Squirrel keyword).
    if (key == "F5") {
        if (eve.dev.isPaused()) {
            eve.dev.continueRun();
            print("dev: continue\n");
        }
        return;
    }
    // F10 = step over (next statement, skip call bodies).
    if (key == "F10" && eve.dev.isPaused()) {
        eve.dev.stepOver();
        print("dev: step over\n");
        return;
    }
    // F11 = step into (next statement, enter calls).
    if (key == "F11" && eve.dev.isPaused()) {
        eve.dev.stepInto();
        print("dev: step into\n");
        return;
    }
    // F8 = step one game frame (secondary; statement stepping is primary).
    if (key == "F8" && eve.dev.isPaused()) {
        eve.dev.stepFrame();
        print("dev: step frame\n");
        return;
    }
    // F6 / F7 = save / load script-state snapshot.
    if (key == "F6") {
        local r = eve.dev.saveSnapshot("eve_snapshot.json");
        print("dev: saveSnapshot -> " + r + "\n");
        return;
    }
    if (key == "F7") {
        local r = eve.dev.loadSnapshot("eve_snapshot.json");
        print("dev: loadSnapshot -> " + r + "\n");
        return;
    }
    // F9 = toggle DevTools AI / MCP panel.
    if (key == "F9") {
        eve.dev.ai.toggleVisible();
        print("dev: AI panel " + (eve.dev.ai.isVisible() ? "shown" : "hidden") + "\n");
        return;
    }
    // F4 = toggle DevTools runtime console / log / REPL panel.
    if (key == "F4") {
        eve.dev.console.toggleVisible();
        print("dev: console " + (eve.dev.console.isVisible() ? "shown" : "hidden") + "\n");
    }
}

function dev_draw_ai() {
    if (has_dev())
        eve.dev.ai.draw();
}

function dev_draw_console() {
    if (has_dev())
        eve.dev.console.draw();
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

// On Android, SDL may queue a spurious "quit" while setOrientation recreates
// the surface. Ignore quit for a few frames so a slow eve_init (demo) does not
// instantly exit. Real back-button quits still work after startup settles.
startup_frames <- 45;

// One frame of the game loop. Returns false to stop (quit requested).
// The desktop loop below calls this; the browser build returns to C++, which
// drives it from emscripten_set_main_loop (requestAnimationFrame).
eve_frame <- function() {
    if (!("_startup_first_frame" in getroottable())) {
        getroottable()._startup_first_frame <- true;
        _startup_ms("first frame begins");
        _log("frame: first frame begins");
    }
    // Coarse "how far did the loop get" marker: log every 300 frames so a crash
    // log reveals the approximate frame the process reached.
    if (!("_frame_count" in getroottable())) getroottable()._frame_count <- 0;
    getroottable()._frame_count += 1;
    if ((getroottable()._frame_count % 300) == 0)
        _log("frame: reached frame " + getroottable()._frame_count);
    local running = true;
    dev_scenario_frame();
    platform_event.pump();
    while (true) {
        local name = platform_event.poll();
        if (name == "") break;
        local data = platform_event.getLastData();
        if ("async_dispatch_event" in getroottable())
            async_dispatch_event(name, data);
        if (name == "keypressed") {
            // getLastData() is the key name (first string arg of the Message).
            handle_dev_key(data, data);
        }
        if (name == "quit") {
            if (startup_frames <= 0)
                running = false;
        }
    }
    if (startup_frames > 0)
        startup_frames -= 1;

    // Rotation / foldable: keep gameplay bounds aligned with the graphics viewport.
    config.width = win.getWidth();
    config.height = win.getHeight();

    poll_hot_reload();
    poll_remote_reload();
    dev_poll();

    local dt = has_module("timer") ? timer.step() : 0.016;
    try {
        // Timers + Promise microtasks before game logic (Node-like macrotask boundary).
        if ("async_pump" in getroottable())
            async_pump();
        if (dev_should_update()) {
            // Playground pause: the page sets the eve_playground_paused root
            // flag to freeze game logic while the render keeps presenting.
            if (!("eve_playground_paused" in getroottable()) || !eve_playground_paused) {
                eve_update(dt);
                // Flush reactions scheduled during eve_update.
                if ("async_pump" in getroottable())
                    async_pump();
            }
            dev_notify_frame_done();
        }
        eve_render();
        // ImGui AI/MCP panel (requires ui.beginFrameAndRender in eve_render).
        dev_draw_ai();
        dev_draw_console();
    } catch (e) {
        if ("dev" in eve) eve.dev.reportError("" + e);
        print("frame error: " + e + "\n");
    }
    // Always present: eve_render may have opened a 3D pass (gfx.render3D)
    // before throwing. Skipping present leaves swapchainPassOpen and every
    // later frame fails with "begin3DFrame: swapchain pass already open".
    try {
        gfx.present();
        if (has_module("ui")) ui.dispatchEvents();
    } catch (e) {
        print("present error: " + e + "\n");
    }
    if (!("_startup_first_present" in getroottable())) {
        getroottable()._startup_first_present <- true;
        _startup_ms("first present - window shows content");
        _log("frame: first present");
    }
    if ("bootBench" in eve && eve.bootBench)
        return false;
    return running;
};

// The browser build has no blocking loop: C++ picks eve_frame up from the root
// table and hands it to emscripten_set_main_loop.
if (!("hostDrivesFrames" in eve) || !eve.hostDrivesFrames) {
    while (eve_frame()) {
    }
    eve_quit();
    win.close();
}
