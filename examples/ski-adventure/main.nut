// White Horizon: native downhill physics with one simulation owner.
dofile("scripts/course.nut");
dofile("scripts/game.nut");
dofile("scripts/render.nut");

local game = null, renderer = null;
local previous = { jump = false, pause = false, restart = false };
local agentHeld = { tuck = false, flip = false, brake = false };
ski <- {};

function publishSki() {
    ski = game.snapshot();
    ski.drawCalls <- renderer == null ? 0 : renderer.drawCalls;
}
/** @brief Main-thread held input, released explicitly or on restart. */
function skiTuck(enabled) { agentHeld.tuck = enabled; }
/** @brief Main-thread player-equivalent rotation input. */
function skiFlip(enabled) { agentHeld.flip = enabled; }
/** @brief Main-thread player-equivalent brake input. */
function skiBrake(enabled) { agentHeld.brake = enabled; }
/** @brief Toggle the same pause state as P; native physics freezes while paused. */
function skiPause() { game.togglePause(); publishSki(); }
/** @brief Reset the existing world and held controls without reconstructing it. */
function skiRestart() {
    agentHeld.tuck = false; agentHeld.flip = false; agentHeld.brake = false;
    game.reset("riding");
    publishSki();
}
/** @brief Queue the same jump as Space; simulation owns buffer/coyote admission. */
function skiJump() {
    if (game.phase == "ready" || game.phase == "won" || game.phase == "lost") skiRestart();
    else { game.pressJump(); publishSki(); }
}

eve_init = function() {
    gfx.setBackgroundColor(0.67, 0.82, 0.90, 1.0);
    game = SkiGame(SkiCourse());
    renderer = SkiRenderer();
    publishSki();
    print("SKI_ADVENTURE_READY native downhill / physical ramps / buffered jump\n");
};
eve_update = function(dt) {
    local jump = keyboard.isDown("space") || keyboard.isDown("up") || keyboard.isDown("w");
    local pause = keyboard.isDown("p"), restart = keyboard.isDown("r");
    if (restart && !previous.restart) skiRestart();
    if (pause && !previous.pause) skiPause();
    if (jump && !previous.jump) skiJump();
    previous.jump = jump; previous.pause = pause; previous.restart = restart;
    game.controls.tuck = agentHeld.tuck || keyboard.isDown("down") || keyboard.isDown("s");
    game.controls.brake = agentHeld.brake || keyboard.isDown("left") || keyboard.isDown("a");
    game.controls.flip = agentHeld.flip || jump;
    game.update(dt);
    publishSki();
};
eve_render = function() { renderer.draw(game); ski.drawCalls <- renderer.drawCalls; };
