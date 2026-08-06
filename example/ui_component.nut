// Declarative UIComponent demo (use as main.nut).

class HudPanel extends eve.UIComponent {
    hp = 100
    volume = 0.5
    constructor(uiRef) {
        base.constructor(uiRef)
        hp = 100
        volume = 0.5
    }
    function build() {
        local u = ui()
        u.beginWindow("HUD", "root")
        u.text("HP " + hp, "hp")
        u.progress(hp / 100.0, "bar", hp + "%")
        u.slider("Vol", volume, 0.0, 1.0, "vol")
        u.button("Hurt", "hurt")
        u.end()
    }
}

eve_init = function() {
    hud <- HudPanel(ui)
    hud.mountAs("hud")
};

eve_update = function(dt) {
    local c = ui.consumeClick();
    while (c != "") {
        if (c == "hud/hurt") {
            hud.hp -= 10;
            if (hud.hp < 0) hud.hp = 0;
            hud.setState();
            hud.updateIfDirty();
        }
        c = ui.consumeClick();
    }
    local ch = ui.consumeChange();
    while (ch != "") {
        if (ch == "hud/vol") {
            hud.volume = ui.getValue("vol");
        }
        ch = ui.consumeChange();
    }
};

eve_render = function() {
    gfx.clear();
    ui.beginFrameAndRender();
};
