// Multi-host ECS UI demo (use as main.nut).
// Demonstrates unified theme switching (dark/light share geometry & fonts).

eve_init = function() {
    ui.setTheme("dark");
    ui.setNavKeyboard(true);

    ui.beginBuild();
    ui.beginWindow("Inventory", "root");
    ui.text("Theme: " + ui.getTheme(), "status");
    ui.button("Use", "use");
    ui.button("Toggle Theme", "theme");
    ui.end();
    ui.mountBuildAs("inv");
};

eve_update = function(dt) {
    local c = ui.consumeClick();
    while (c != "") {
        if (c == "inv/use") {
            ui.setText("status", "Used (" + ui.getTheme() + ")");
        } else if (c == "inv/theme") {
            if (ui.getTheme() == "dark") ui.setTheme("light");
            else ui.setTheme("dark");
            ui.setText("status", "Theme: " + ui.getTheme());
        }
        c = ui.consumeClick();
    }
};

eve_render = function() {
    gfx.clear();
    ui.beginFrameAndRender();
};
