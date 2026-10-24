dofile("util.nut");
dofile("behavior.nut");
dofile("skills.nut");
dofile("eqs.nut");
dofile("planner.nut");
dofile("simulation.nut");
dofile("presentation.nut");
dofile("verification.nut");

local simulation = null;
local accumulator = 0.0;
local previous = {};

squadObservation <- {
    tick = 0,
    command = "BALANCED",
    auto = true,
    bossHp = 900.0,
    playerHp = 140.0,
    eqs_queries = 0,
    gas_hits = 0,
    heals = 0,
    dodges = 0,
    tests = "not-run"
};

game <- {};
game.reset <- function() {
    if (simulation != null) simulation.close();
    simulation = SquadBossSimulation();
    accumulator = 0.0;
};
game.balanced <- function() { simulation.order("BALANCED"); };
game.focus <- function() { simulation.order("FOCUS"); };
game.protect <- function() { simulation.order("PROTECT"); };
game.retreat <- function() { simulation.order("RETREAT"); };
game.toggleAuto <- function() { simulation.auto = !simulation.auto; };
game.verify <- function() {
    sbVerify();
    squadObservation.tests = "passed";
};

function sbPressed(key) {
    local down = keyboard.isDown(key);
    local was = key in previous ? previous[key] : false;
    previous[key] <- down;
    return down && !was;
}

eve_init <- function() {
    sbPresentationInit();
    game.reset();
    print("SQUAD_BOSS_3D_READY eqs=sensing gas=action/combat/attributes/effects ai=npc_ai\n");
};

eve_update <- function(dt) {
    if (sbPressed("1")) game.balanced();
    if (sbPressed("2")) game.focus();
    if (sbPressed("3")) game.protect();
    if (sbPressed("4")) game.retreat();
    if (sbPressed("p")) game.toggleAuto();
    if (sbPressed("r")) game.reset();
    if (sbPressed("v")) game.verify();
    if (sbPressed("j")) simulation.activatePlayerSkill(0);
    if (sbPressed("k")) simulation.activatePlayerSkill(1);
    if (sbPressed("l")) simulation.activatePlayerSkill(2);

    local input = { x = 0.0, z = 0.0, attack = false };
    if (keyboard.isDown("a") || keyboard.isDown("left")) input.x -= 1.0;
    if (keyboard.isDown("d") || keyboard.isDown("right")) input.x += 1.0;
    if (keyboard.isDown("w") || keyboard.isDown("up")) input.z -= 1.0;
    if (keyboard.isDown("s") || keyboard.isDown("down")) input.z += 1.0;

    accumulator += dt;
    local steps = 0;
    while (accumulator >= 1.0 / 60.0 && steps < 12) {
        simulation.step(1.0 / 60.0, input);
        accumulator -= 1.0 / 60.0;
        steps += 1;
    }

    squadObservation.tick = simulation.tick;
    squadObservation.command = simulation.command;
    squadObservation.auto = simulation.auto;
    squadObservation.bossHp = simulation.units[simulation.bossId].hp;
    squadObservation.playerHp = simulation.units[0].hp;
    squadObservation.eqs_queries = simulation.stats.eqs_queries;
    squadObservation.gas_hits = simulation.stats.gas_hits;
    squadObservation.heals = simulation.stats.heals;
    squadObservation.dodges = simulation.stats.dodges;
    sbUpdatePresentation(simulation);
};

eve_render <- function() {
    gfx.clear();
    gfx.render3D();
    if (simulation != null) sbRenderHud(simulation);
};
