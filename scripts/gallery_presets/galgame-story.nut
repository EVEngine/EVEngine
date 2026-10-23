// Use the normal Start and Advance actions until the first character dialogue.
startGame();
local storyUpdate = eve_update;
eve_update = function(dt) {
    storyUpdate(dt);
    if (gal.mode == "game" && gal.step < 2) advanceStory();
};
