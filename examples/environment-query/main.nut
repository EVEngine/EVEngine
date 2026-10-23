// The query proposes locations; the consumer selects and moves the blue unit.
local sensing = null;
local world = null;
local candidates = [];
local selected = null;
local unitX = 120.0;
local unitY = 300.0;
local targetX = 550.0;
local targetY = 300.0;

eve_init <- function() {
    sensing = eve.Sensing();
    local created = sensing.newWorld();
    if (!created.ok) throw "could not create sensing world";
    world = created.value;
    // No LOS backend is required by this geometric demonstration.
    local queried = world.queryPositions(unitX, unitY, targetX, targetY, 120.0, 24, 24, false);
    if (!queried.ok) throw "environment query failed";
    candidates = queried.value.ranked;
    if (candidates.len() > 0) selected = candidates[0].location;
    local debug = world.debugLastQueryJson();
    if (!debug.ok) throw "query diagnostics unavailable";
    print("environment-query: " + candidates.len() + " ranked locations\n");
    gfx.setBackgroundColor(0.06, 0.08, 0.12, 1.0);
};

eve_update <- function(dt) {
    if (selected == null) return;
    local step = dt * 1.5;
    if (step > 1.0) step = 1.0;
    unitX += (selected.x - unitX) * step;
    unitY += (selected.y - unitY) * step;
};

eve_render <- function() {
    gfx.clear();
    foreach (candidate in candidates) {
        local p = candidate.location;
        gfx.drawSolidRect(p.x - 4.0, p.y - 4.0, 8.0, 8.0,
            0.1, 0.25 + candidate.score * 0.7, 0.2, 1.0);
    }
    gfx.drawSolidRect(targetX - 12.0, targetY - 12.0, 24.0, 24.0, 0.95, 0.3, 0.2, 1.0);
    gfx.drawSolidRect(unitX - 9.0, unitY - 9.0, 18.0, 18.0, 0.2, 0.65, 1.0, 1.0);
};
