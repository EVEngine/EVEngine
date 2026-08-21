// Reflection-driven property inspector demo (MVVM DevTools panel).
//
// Script classes + Squirrel attribute metadata are auto-scanned; the panel
// binds to the live `hero` instance two-way: widget edits write back to the
// model, and sync() pulls external model changes into the view each frame.

if (!("hero" in getroottable())) hero <- null;
if (!("tick" in getroottable())) tick <- 0.0;

class WeaponData {
    name = "Sword"
    damage = 12.0
    level = 1
    equipped = true
    kind = "melee"
}

class CharacterData extends WeaponData {
    </ editor = "slider", min = 0, max = 1000 />
    hp = 100.0
    </ editor = "slider", min = 0, max = 100 />
    stamina = 80.0
    characterName = "Hero"
    alive = true
    </ editor = "combo", options = "warrior,mage,rogue" />
    job = "warrior"
    skills = []

    constructor(name = "Hero", hpValue = 100.0) {
        characterName = name
        hp = hpValue
    }
}

eve_init = function() {
    if (hero == null) hero = CharacterData("Hero", 100.0);
    ui.setTheme("dark");
    ui.inspect();            // auto-scan script classes + open the panel
    ui.inspectObject(hero);  // bind the panel to the live hero object
};

eve_update = function(dt) {
    // External model change: the inspector's per-frame sync() pushes it into
    // the property panel, so the View always mirrors the ViewModel.
    tick += dt;
    if (tick >= 1.0) {
        tick = 0.0;
        if (hero.stamina < 100.0) hero.stamina += 1.0;
    }
};

eve_render = function() {
    gfx.clear();
    ui.beginFrameAndRender();
};
