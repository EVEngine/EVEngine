// Runtime state is rebuilt on reload. C++ CombatRuntime is the sole health/position authority.
dofile("arena.nut");
dofile("render.nut");

arena <- null;
numberView <- null;
fontData <- null;
hudFont <- null;
actors <- [];
player <- null;
effects <- [];
commands <- { attack = false, heavy = false, dodge = false, x = 0.0, y = 0.0 };
keyHistory <- {};
gameState <- {};
fixedRemainder <- 0.0;
viewScale <- 1.0;
viewX <- 0.0;
viewY <- 0.0;

function arenaMin(a, b) { return a < b ? a : b; }
function arenaMax(a, b) { return a > b ? a : b; }
function checked(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
}
function pressed(key) {
    local now = keyboard.isDown(key);
    local before = key in keyHistory ? keyHistory[key] : false;
    keyHistory[key] <- now;
    return now && !before;
}
function resetArena() {
    arena = checked(combat.newRuntime());
    numberView = checked(gfx.newDamageNumbers());
    actors = []; effects = []; keyHistory = {}; fixedRemainder = 0.0;
    commands = { attack = false, heavy = false, dodge = false, x = 0.0, y = 0.0 };
    gameState = { tick = 0, time = 0.0, wave = 0, kills = 0, hits = 0, criticals = 0,
        dodges = 0, avoided = 0, playerHealth = 320.0, enemyCount = 0, popupCount = 0,
        lastApplied = 0.0, lastRequested = 0.0, lastStages = 0, phase = "playing",
        demo = false, nextWave = 0.6, paused = false };
    player = fighter(1, 640.0, 385.0, true, 320.0);
    foreach (kind in ["damage", "critical", "heal", "miss"]) {
        local style = eve.DamageNumberStyle();
        style.scale = viewScale * (kind == "critical" ? 1.3 : 0.9);
        style.velocityY = -95.0 * viewScale;
        style.gravity = 45.0 * viewScale;
        style.outline = viewScale;
        style.mergeRadius = 64.0 * viewScale;
        if (kind == "critical") { style.g = 0.8; style.b = 0.18; style.shake = 4.0 * viewScale; }
        if (kind == "heal") { style.r = 0.2; style.g = 1.0; style.b = 0.7; style.gravity = 0.0; }
        if (kind == "miss") { style.r = 0.55; style.g = 0.8; style.b = 1.0; }
        checked(numberView.setStyle(kind, style));
    }
}

game <- {};
game.reset <- function() { resetArena(); };
game.demo <- function() { resetArena(); gameState.demo = true; };
game.attack <- function() { commands.attack = true; };
game.heavy <- function() { commands.heavy = true; };
game.dodge <- function() { commands.dodge = true; };
game.right <- function() { commands.x = 1.0; commands.y = 0.0; };
game.left <- function() { commands.x = -1.0; commands.y = 0.0; };
game.up <- function() { commands.x = 0.0; commands.y = -1.0; };
game.down <- function() { commands.x = 0.0; commands.y = 1.0; };
game.stop <- function() { commands.x = 0.0; commands.y = 0.0; };

eve_init <- function() {
    gfx.setBackgroundColor(0.025, 0.035, 0.06, 1.0);
    viewScale = arenaMin(gfx.getWidth() / 1280.0, gfx.getHeight() / 720.0);
    viewX = (gfx.getWidth() - 1280.0 * viewScale) * 0.5;
    viewY = (gfx.getHeight() - 720.0 * viewScale) * 0.5;
    // Keep decoded font alive for the entire GPU font lifetime.
    fontData = eve.Font().newFontDataFromFile("assets/fonts/DejaVuSans-Bold.ttf", 28);
    hudFont = gfx.newFont(fontData, " ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789=+-!./:[]()%");
    resetArena();
};
eve_reload <- function() { eve_init(); };
eve_update <- function(dt) {
    if (pressed("r")) resetArena();
    if (pressed("tab")) { resetArena(); gameState.demo = true; }
    if (pressed("p")) gameState.paused = !gameState.paused;
    if (gameState.paused) return;
    if (pressed("j")) commands.attack = true;
    if (pressed("k")) commands.heavy = true;
    if (pressed("space")) commands.dodge = true;
    fixedRemainder += arenaMin(dt, 0.1);
    while (fixedRemainder >= 1.0 / 60.0) {
        simulateArena(1.0 / 60.0);
        fixedRemainder -= 1.0 / 60.0;
    }
};
eve_render <- function() { renderArena(); };
