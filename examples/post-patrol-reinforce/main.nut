dofile("simulation.nut");

persist labels = []

function pushLabel(text) {
    labels.append(text);
    if (labels.len() > 8) labels.remove(0);
}

eve_init <- function() {
    gfx.setBackgroundColor(0.07, 0.09, 0.12, 1.0);
    try {
        runContract();
        pushLabel("PASS " + sim.note);
        print("post-patrol-reinforce: PASS phase=" + sim.phase + " def=" + sim.lastDef + "\n");
    } catch (error) {
        sim.failed = true;
        sim.note = error.tostring();
        pushLabel("FAIL " + sim.note);
        print("post-patrol-reinforce: FAIL " + sim.note + "\n");
    }
}

eve_update <- function(dt) {
    // Contract is one-shot at init; keep the window alive for smoke.
}

eve_render <- function() {
    gfx.clear();
    // Gate posts
    local filled = sim.postOccupancy;
    gfx.drawSolidRect(120.0, 180.0, 70.0, 120.0, filled >= 1 ? 0.25 : 0.45, filled >= 1 ? 0.55 : 0.22, 0.28, 1.0);
    gfx.drawSolidRect(210.0, 180.0, 70.0, 120.0, filled >= 2 ? 0.25 : 0.45, filled >= 2 ? 0.55 : 0.22, 0.28, 1.0);
    // Patrol squad strip
    local squadColor = sim.intent == "patrol" ? 0.35 : 0.2;
    gfx.drawSolidRect(420.0, 220.0, 40.0 + sim.squadSize * 50.0, 40.0, 0.2, squadColor, 0.55, 1.0);

    local y = 40.0;
    gfx.drawSolidRect(40.0, y, 880.0, 28.0, 0.12, 0.14, 0.18, 1.0);
    // Status blocks as colored bars (no font module required).
    local ok = sim.passed && !sim.failed;
    gfx.drawSolidRect(48.0, y + 6.0, ok ? 220.0 : 120.0, 16.0, ok ? 0.2 : 0.55, ok ? 0.65 : 0.25, 0.3, 1.0);
    local i = 0;
    while (i < labels.len()) {
        local shade = 0.18 + i * 0.03;
        gfx.drawSolidRect(48.0, 360.0 + i * 18.0, 20.0 + labels[i].len() * 7.0, 12.0, shade, shade + 0.05, shade + 0.08, 1.0);
        i += 1;
    }
}
