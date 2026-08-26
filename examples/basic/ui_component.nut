// Declarative UIComponent demo (use as main.nut).

class HealthReadout extends eve.UIComponent {
    function build() {
        local u = ui()
        u.text("HP " + props.hp, "hp")
        u.progress(props.hp / 100.0, "bar", props.hp + "%")
    }
}

class HudPanel extends eve.UIComponent {
    readout = null
    constructor(uiRef) {
        base.constructor(uiRef, { title = "HUD" })
        state.hp <- 100
        state.volume <- 0.5
        readout = HealthReadout(uiRef)
    }
    function build() {
        local u = ui()
        u.beginWindow(props.title, "root")
        renderChild(readout, { hp = state.hp })
        u.slider("Vol", state.volume, 0.0, 1.0, "vol")
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
            local nextHp = hud.state.hp - 10;
            if (nextHp < 0) nextHp = 0;
            hud.setState({ hp = nextHp });
            hud.updateIfDirty();
        }
        c = ui.consumeClick();
    }
    local ch = ui.consumeChange();
    while (ch != "") {
        if (ch == "hud/vol") {
            hud.setState({ volume = ui.getValue("vol") });
        }
        ch = ui.consumeChange();
    }
};

eve_render = function() {
    gfx.clear();
    ui.beginFrameAndRender();
};
