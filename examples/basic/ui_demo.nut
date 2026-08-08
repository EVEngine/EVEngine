// Multi-host ECS UI demo (use as main.nut).

eve_init = function() {
    ui.beginBuild();
    ui.beginWindow("Inventory", "root");
    ui.text("Ready", "status");
    ui.button("Use", "use");
    ui.end();
    ui.mountBuildAs("inv");
};

eve_update = function(dt) {
    local c = ui.consumeClick();
    while (c != "") {
        if (c == "inv/use") ui.setText("status", "Used");
        c = ui.consumeClick();
    }
};

eve_render = function() {
    gfx.clear();
    ui.beginFrameAndRender();
};
