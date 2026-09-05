// ============================================================================
// AI Game — 一个能被 AI Agent 通过 MCP 直接驾驶的最小可玩对战游戏。
//
// 启动（Agent 端口）：
//   eve run --debug --mcp-port=7529 examples/ai-game
// 或：make run/<platform>-debug GAME=examples/ai-game RUN_ARGS="--debug --mcp-port=7529"
//
// Agent 通过 eve_play 驾驶本游戏（见 game.agent.json）：
//   status / clock / step / observe combat-alive / capture / checkpoint
// 不要把 eve_eval 当作官方观察路径。
// 一键复现：python examples/ai-game/agent_demo.py 7529
// ============================================================================

persist gameState = {
        tick = 0
        time = 0.0
        hits = 0
        plaTimer = 0.0
        eneTimer = 0.0
        player = { hp = 100.0, maxHp = 100.0, attack = 14.0 }
        enemy  = { hp = 80.0,  maxHp = 80.0,  attack = 9.0 }
    }

// --- Agent 可调用的脚本入口（命令与可序列化权威状态保持分离） ---
game <- {};
game.setEnemyHp <- function(v) { gameState.enemy.hp = v.tofloat(); };
game.setPlayerHp <- function(v) { gameState.player.hp = v.tofloat(); };
game.reset <- function() {
    gameState.player.hp = gameState.player.maxHp;
    gameState.enemy.hp = gameState.enemy.maxHp;
    gameState.hits = 0;
    gameState.plaTimer = 0.0;
    gameState.eneTimer = 0.0;
};

eve_init = function() {
    // `persist` preserves this root across hot reload; DevTools registration
    // additionally makes it part of explicit MCP snapshot capture/restore.
    if ("dev" in eve) eve.dev.markStateRoot("gameState");
    gfx.setBackgroundColor(0.07, 0.08, 0.12, 1.0);
    ui.setTheme("dark");
    ui.beginBuild();
    ui.beginWindow("AI Game", "root");
    ui.text("", "status");
    ui.text("", "hint");
    ui.end();
    ui.mountBuildAs("hud");
    ui.select("hud");
    ui.setHostOverlay(true);
    ui.setHostPos(12.0, 12.0, 0.0, 0.0);
};

eve_update = function(dt) {
    gameState.tick += 1;
    gameState.time += dt;
    gameState.plaTimer += dt;
    gameState.eneTimer += dt;

    if (gameState.player.hp > 0.0 && gameState.enemy.hp > 0.0) {
        // 玩家自动攻击 + 缓慢回复。
        if (gameState.plaTimer >= 0.8) {
            gameState.plaTimer = 0.0;
            gameState.enemy.hp -= gameState.player.attack;
            gameState.hits += 1;
        }
        gameState.player.hp += 2.0 * dt;
        if (gameState.player.hp > gameState.player.maxHp) gameState.player.hp = gameState.player.maxHp;
        // 敌人反击。
        if (gameState.eneTimer >= 1.4) {
            gameState.eneTimer = 0.0;
            gameState.player.hp -= gameState.enemy.attack;
        }
    }
    if (gameState.player.hp < 0.0) gameState.player.hp = 0.0;
    if (gameState.enemy.hp < 0.0) gameState.enemy.hp = 0.0;

    ui.select("hud");
    local state = format("tick=%d  time=%.1f  hits=%d", gameState.tick, gameState.time, gameState.hits);
    local hpText = format("玩家 HP %.0f/%.0f   敌人 HP %.0f/%.0f",
                          gameState.player.hp, gameState.player.maxHp, gameState.enemy.hp, gameState.enemy.maxHp);
    ui.setText("status", state + "\n" + hpText);
    local hint = gameState.player.hp <= 0.0
        ? "GAME OVER —— Agent 可用 eve_run_script: game.reset(); 重开"
        : "MCP: eve_play status/observe/step/capture/checkpoint";
    ui.setText("hint", hint);
};

eve_render = function() {
    gfx.clear();

    // 玩家（左，蓝色）与敌人（右，红色）血条。
    local pw = 300.0;
    gfx.drawSolidRect(60.0, 300.0, pw, 22.0, 0.18, 0.20, 0.26, 1.0);
    gfx.drawSolidRect(60.0, 300.0, pw * (gameState.player.hp / gameState.player.maxHp), 22.0, 0.30, 0.65, 0.95, 1.0);
    gfx.drawSolidRect(600.0, 300.0, pw, 22.0, 0.20, 0.16, 0.18, 1.0);
    gfx.drawSolidRect(600.0, 300.0, pw * (gameState.enemy.hp / gameState.enemy.maxHp), 22.0, 0.90, 0.32, 0.30, 1.0);

    gfx.drawSolidRect(140.0, 250.0, 70.0, 90.0, 0.28, 0.52, 0.85, 1.0);
    gfx.drawSolidRect(760.0, 250.0, 70.0, 90.0, 0.82, 0.28, 0.26, 1.0);

    ui.beginFrameAndRender();
};
