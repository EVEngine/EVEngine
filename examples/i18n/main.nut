// ============================================================================
// EVEngine i18n demo — game localization end-to-end.
//
//   eve.I18n  ——  JSON locale tables, dot-path lookup, {name} placeholders,
//                plural rules, default-language fallback, file hot-reload.
//
// Locale files: locales/en.json, locales/zh.json, locales/fr.json.
//
// 操作：数字键 1 / 2 / 3 切换语言，空格切换自动热重载（改 zh.json 看效果）。
// 运行：make run/macosx-debug GAME=examples/i18n
// ============================================================================

persist i18n = eve.I18n()
persist uiReady = false
persist prevKeys = {}
persist loaded = false

function edgePressed(key, down) {
    local was = (key in prevKeys) ? prevKeys[key] : false;
    prevKeys[key] <- down;
    return down && !was;
}

function keyPressed(name) {
    return edgePressed("k_" + name, keyboard.isDown(name));
}

function loadLocales() {
    if (i18n.hasLanguage("en")) return;  // protect across hot-reload
    i18n.loadFromFile("en", "locales/en.json");
    i18n.loadFromFile("zh", "locales/zh.json");
    i18n.loadFromFile("fr", "locales/fr.json");
    i18n.setDefaultLanguage("en");
    i18n.setLanguage("zh");
    i18n.setAutoReload(true);
    loaded = true;
}

function langLabel(lang) {
    if (lang == "zh") return "中文";
    if (lang == "fr") return "Français";
    return "English";
}

function refresh() {
    if (!uiReady || !loaded) return;
    ui.select("root");
    local lang = i18n.getLanguage();
    local reloaded = i18n.update(0.0);

    ui.setText("title", i18n.get("menu.title"));
    ui.setText("lang", i18n.getWithParams("status.language", { lang = langLabel(lang) }));
    ui.setText("reload", i18n.getWithParams("status.reload",
        { on = i18n.isAutoReload() ? "ON" : "OFF" }));

    ui.setText("greeting", i18n.getWithParams("greeting", { name = "EVEngine" }));
    ui.setText("items1", i18n.getPlural("items", 1));
    ui.setText("items5", i18n.getPlural("items", 5));
    ui.setText("apples", i18n.getPlural("apples", 21));

    ui.setText("b1", "1. " + i18n.get("buttons.start"));
    ui.setText("b2", "2. " + i18n.get("buttons.quit"));
    ui.setText("b3", "3. " + i18n.get("buttons.continue"));

    ui.setText("hint", i18n.get("menu.hint"));
    if (reloaded > 0)
        ui.setText("reloaded", i18n.getWithParams("status.reloaded", { n = reloaded }));
    else
        ui.setText("reloaded", "");
}

function buildUI() {
    ui.beginBuild();
    ui.beginWindow("i18n", "root");
    ui.text("", "title");
    ui.separator("sep0");
    ui.text("", "greeting");
    ui.text("", "items1");
    ui.text("", "items5");
    ui.text("", "apples");
    ui.separator("sep1");
    ui.text("", "b1");
    ui.text("", "b2");
    ui.text("", "b3");
    ui.separator("sep2");
    ui.text("", "lang");
    ui.text("", "reload");
    ui.text("", "reloaded");
    ui.text("", "hint");
    ui.end();
    ui.mountBuildAs("root");
    ui.setHostOverlay(true);
    uiReady = true;
}

function eve_init() {
    gfx.setBackgroundColor(0.10, 0.12, 0.16, 1.0);
    loadLocales();
    buildUI();
    refresh();
}

function eve_reload() {
    buildUI();
    refresh();
}

function eve_update(dt) {
    if (keyPressed("1")) i18n.setLanguage("en");
    if (keyPressed("2")) i18n.setLanguage("zh");
    if (keyPressed("3")) i18n.setLanguage("fr");
    if (keyPressed("Space")) i18n.setAutoReload(!i18n.isAutoReload());
    refresh();
}

function eve_render() {
    gfx.clear();
    ui.beginFrameAndRender();
}