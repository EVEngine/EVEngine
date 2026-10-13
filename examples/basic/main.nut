// Change tutorialStep in config.nut, then restart the game.
local lessons = [
    "01-rectangle", "02-image", "03-movement",
    "04-physics", "05-particles", "06-fire"
];
local step = config.tutorialStep;
if (typeof step != "integer" || step < 1 || step > lessons.len())
    throw "basic: tutorialStep must be an integer from 1 to 6";

// Reset callbacks before loading one complete lesson (also on soft reload).
eve_update = function(dt) {};
eve_render = function() { gfx.clear(); };
dofile("steps/" + lessons[step - 1] + ".nut");
eve_reload <- function() { eve_init(); };
print("Basic step " + step + ": " + lessons[step - 1] + "\n");
