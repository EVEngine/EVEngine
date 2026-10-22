// Presentation-only attack study; time is supplied by the engine, never wall clock.
inkTime <- 0.0;
inkMode <- 0;
inkPaused <- false;
inkDark <- false;
inkShader <- null;
inkKeys <- {};
inkTexture <- null;
function inkPressed(key) {
    local down = keyboard.isDown(key);
    local before = key in inkKeys ? inkKeys[key] : false;
    inkKeys[key] <- down;
    return down && !before;
}
eve_init <- function() {
    inkTexture = gfx.newTexture(eve.Image().newImageDataFromFile("assets/splash-flipbook.png"), false, false);
    inkShader = gfx.newShaderFromSpvFile("shaders/combat.frag.spv");
    inkShader.declareVec4("frame");
    inkShader.declareVec4("settings");
};
eve_update <- function(dt) {
    if (inkPressed("1")) { inkMode = 0; inkTime = 0.0; }
    if (inkPressed("2")) { inkMode = 1; inkTime = 0.0; }
    if (inkPressed("3")) { inkMode = 2; inkTime = 0.0; }
    if (inkPressed("space")) inkTime = 0.0;
    if (inkPressed("p")) inkPaused = !inkPaused;
    if (inkPressed("b")) inkDark = !inkDark;
    if (!inkPaused) inkTime = (inkTime + dt) % 2.6;
};
eve_render <- function() {
    gfx.setBackgroundColor(inkDark ? 0.12 : 0.94, inkDark ? 0.16 : 0.92,
        inkDark ? 0.18 : 0.86, 1.0);
    gfx.clear();
    inkShader.sendVec4("frame", gfx.getWidth().tofloat(), gfx.getHeight().tofloat(), inkTime, inkMode.tofloat());
    inkShader.sendVec4("settings", inkDark ? 1.0 : 0.0, 0.0, 0.0, 0.0);
    gfx.setShader(inkShader);
    gfx.drawTexturedRect(inkTexture, 0.0, 0.0, gfx.getWidth().tofloat(), gfx.getHeight().tofloat(), 1.0, 1.0, 1.0, 1.0);
    // This standalone showcase owns the only 2D draw and its shader.
};
