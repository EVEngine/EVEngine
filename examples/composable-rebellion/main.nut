dofile("simulation.nut");

eve_init = function() {
    gfx.setBackgroundColor(0.055, 0.07, 0.10, 1.0);
    run_rebellion_scenario();
    print("rebellion=" + demoState.rebelled +
          " base_owner=" + demoSocial.ownerOf(demoState.baseId) +
          " production=" + demoState.baseStats.getFinal("production_speed", 0.0) + "\n");
};

eve_update = function(dt) {};

eve_render = function() {
    gfx.clear();
    local crown = demoState.rebelled ? 0.18 : 0.65;
    local frontier = demoState.rebelled ? 0.72 : 0.20;
    gfx.drawSolidRect(100.0, 120.0, 300.0, 300.0, crown, 0.20, 0.24, 1.0);
    gfx.drawSolidRect(560.0, 120.0, 300.0, 300.0, 0.18, frontier, 0.42, 1.0);
};
