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
    // Editor shell: icon toolbar + adjustable Inspector / Database / Scene workspace.
    ui.editorOpen();

    // Database panel: register the live hero and one extra default instance.
    ui.dbRegister(hero, "Hero");
    ui.dbCreateInstance();

    // Inspector bound to the live hero object.
    ui.inspectObject(hero);

    // Scene panel Pick -> map the node id to a script instance and inspect it.
    // (Demo: no real scene nodes, so every pick resolves to the hero.)
    ui.sceneSetPickHandler(function(nodeId) {
        ui.inspectObject(hero);
    });
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
