// ============================================================================
// DevLab — 开发者体验实验室
//
// 目标：把 EVEngine「运行时即编辑器」的开发循环浓缩成一个 5 分钟可跑的示例。
// 必须用 --debug 运行（make devlab 已带），否则 DevTools 不加载：
//
//   F4      运行时控制台 / REPL（eve.dev.console.*）
//   F6/F7   保存 / 恢复脚本状态快照（eve.dev.saveSnapshot / loadSnapshot）
//   F9      AI / MCP 面板（eve.dev.ai.*）
//   Pause   暂停游戏循环      F5 继续      F8 单帧
//   F10/F11 语句级单步（step over / step into）
//   空格或「触发运行时错误」按钮 → 制造一个错误，观察 break-on-error 与错误切片
//
// 热重载：修改本文件任意函数后保存，脚本软重载立即生效（lab.reloads +1），
// 小球也会按新公式运动。修改 assets 或 JSON 资源则走 eve_asset_reload。
// ============================================================================

// 热重载持久化：EveScript persist 声明保留已创建的 C++ 对象与累计状态。
persist lab = {
        count = 0
        reloads = 0
        x = 120.0
        y = 180.0
        vx = 96.0
        vy = 60.0
        lastLog = ""
    }

// --- 热重载试验台：改这个函数，保存，看小球运动立刻变化 ---
function bounceDir(v, maxV) {
    // 例如：把 0.85 改成 0.5 会让反弹衰减；改成 1.0 保持速度。
    return (v < 0.0 || v > maxV) ? -0.85 : 1.0;
}

function labLog(text) {
    lab.lastLog = text;
    print("[devlab] " + text + "\n");
    if ("dev" in eve && "console" in eve.dev)
        eve.dev.console.info(text);
}

function devReady() {
    return ("dev" in eve);
}

eve_init = function() {
    gfx.setBackgroundColor(0.07, 0.09, 0.14, 1.0);

    ui.setTheme("dark");
    ui.beginBuild();
    ui.beginWindow("DevLab", "root");
    ui.text("EVEngine DevLab", "title");
    ui.text("", "status");
    ui.text("", "hint");
    ui.button("触发运行时错误", "boom");
    ui.end();
    ui.mountBuildAs("labui");
    ui.select("labui");
    ui.setHostOverlay(true);
    ui.setHostPos(12.0, 12.0, 0.0, 0.0);

    if (devReady()) {
        // 错误即暂停，配合 DAP / MCP 观察调用栈与错误切片。
        eve.dev.setBreakOnError(true);
        eve.dev.ai.note("devlab started");
        labLog("DevTools attached: F4 console / F6-F7 snapshot / F9 AI panel");
    } else {
        labLog("DevTools not attached — run with --debug (make devlab)");
    }
};

// 软重载后（脚本变更）调用：只做增量，不重建窗口 / 实体 / UI。
eve_reload <- function() {
    lab.reloads += 1;
    labLog("script hot-reloaded (" + lab.reloads + " times)");
    if (devReady())
        eve.dev.ai.note("hot reload #" + lab.reloads);
};

// 非 .nut 资源变更后调用（hot.tryReload 已处理的内置资源除外）。
eve_asset_reload <- function(path) {
    labLog("asset changed: " + path);
};

function makeError() {
    // 触发一次脚本错误：--debug 下会停在抛出点（break on error），
    // 并在控制台 / DAP / MCP 生成错误切片，指出是哪一行导致的。
    local missing = lab.nope_missing_field;
    missing += 1;
}

eve_update = function(dt) {
    lab.count += 1;

    // 小球运动：热改 bounceDir 后保存，立即按新行为运动。
    lab.vx *= bounceDir(lab.x, config.width - 48.0);
    lab.vy *= bounceDir(lab.y, config.height - 48.0);
    lab.x += lab.vx * dt;
    lab.y += lab.vy * dt;

    // 输入：空格触发演示错误。
    if (keyboard.isDown("Space") || keyboard.isDown("space"))
        makeError();

    // UI 按钮：触发演示错误。
    local c = ui.consumeClick();
    while (c != "") {
        if (c == "labui/boom")
            makeError();
        c = ui.consumeClick();
    }

    // HUD 刷新（每帧低频率文本更新，安全）。
    ui.select("labui");
    local paused = devReady() && eve.dev.isPaused() ? " [PAUSED]" : "";
    ui.setText("status",
        "frames=" + lab.count + "  reloads=" + lab.reloads +
        "  pos=(" + lab.x.tointeger() + "," + lab.y.tointeger() + ")" + paused);
    local hint =
        devReady()
            ? "F4 控制台 · F6/F7 快照 · F9 AI 面板 · Pause 暂停 · Space 触发错误"
            : "DevTools 未加载：请用 make devlab（--debug）运行";
    ui.setText("hint", hint);
};

eve_render = function() {
    gfx.clear();
    gfx.drawSolidRect(lab.x - 20.0, lab.y - 20.0, 40.0, 40.0, 0.32, 0.78, 0.95, 1.0);
    gfx.drawSolidRect(0.0, config.height - 48.0, config.width.tofloat(), 48.0, 0.16, 0.18, 0.24, 1.0);
    ui.beginFrameAndRender();
};
