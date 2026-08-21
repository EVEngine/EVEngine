// ============================================================================
// AI Game — 一个能被 AI Agent 通过 MCP 直接驾驶的最小可玩对战游戏。
//
// 启动（Agent 端口）：
//   eve run --debug --mcp-port=7529 examples/ai-game
// 或：make run/<platform>-debug GAME=examples/ai-game RUN_ARGS="--debug --mcp-port=7529"
//
// Agent 可以像人一样对这个运行中的游戏：
//   读状态  eve_eval        game.enemy.hp / game.player.hp / game.tick
//   改状态  eve_run_script  game.setEnemyHp(20.0); game.reset();
//   复位    eve_snapshot_capture / eve_snapshot_restore
//   看见    eve_screenshot  （配合 eve_render_describe 可让视觉模型描述画面）
//   暂停    eve_pause / eve_continue / eve_step_frame
//
// 一键复现：python examples/ai-game/agent_demo.py 7529
// ============================================================================

game <- persist("game", function() {
    return {
        tick = 0
        time = 0.0
        hits = 0
        plaTimer = 0.0
        eneTimer = 0.0
        player = { hp = 100.0, maxHp = 100.0, attack = 14.0 }
        enemy  = { hp = 80.0,  maxHp = 80.0,  attack = 9.0 }
    };
});

// --- Agent 可调用的脚本入口（MCP eve_run_script / eve_eval 均可触达） ---
game.setEnemyHp <- function(v) { game.enemy.hp = v.tofloat(); };
game.setPlayerHp <- function(v) { game.player.hp = v.tofloat(); };
game.reset <- function() {
    game.player.hp = game.player.maxHp;
    game.enemy.hp = game.enemy.maxHp;
    game.hits = 0;
    game.plaTimer = 0.0;
    game.eneTimer = 0.0;
};

eve_init = function() {
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
    game.tick += 1;
    game.time += dt;
    game.plaTimer += dt;
    game.eneTimer += dt;

    if (game.player.hp > 0.0 && game.enemy.hp > 0.0) {
        // 玩家自动攻击 + 缓慢回复。
        if (game.plaTimer >= 0.8) {
            game.plaTimer = 0.0;
            game.enemy.hp -= game.player.attack;
            game.hits += 1;
        }
        game.player.hp += 2.0 * dt;
        if (game.player.hp > game.player.maxHp) game.player.hp = game.player.maxHp;
        // 敌人反击。
        if (game.eneTimer >= 1.4) {
            game.eneTimer = 0.0;
            game.player.hp -= game.enemy.attack;
        }
    }
    if (game.player.hp < 0.0) game.player.hp = 0.0;
    if (game.enemy.hp < 0.0) game.enemy.hp = 0.0;

    ui.select("hud");
    local state = format("tick=%d  time=%.1f  hits=%d", game.tick, game.time, game.hits);
    local hpText = format("玩家 HP %.0f/%.0f   敌人 HP %.0f/%.0f",
                          game.player.hp, game.player.maxHp, game.enemy.hp, game.enemy.maxHp);
    ui.setText("status", state + "\n" + hpText);
    ui.setText("hint",
        game.player.hp <= 0.0
            ? "GAME OVER —— Agent 可用 eve_run_script: game.reset(); 重开"
            : "MCP: eve_eval / eve_run_script / eve_screenshot / eve_snapshot_*");
};

eve_render = function() {
    gfx.clear();

    // 玩家（左，蓝色）与敌人（右，红色）血条。
    local pw = 300.0;
    gfx.drawSolidRect(60.0, 300.0, pw, 22.0, 0.18, 0.20, 0.26, 1.0);
    gfx.drawSolidRect(60.0, 300.0, pw * (game.player.hp / game.player.maxHp), 22.0, 0.30, 0.65, 0.95, 1.0);
    gfx.drawSolidRect(600.0, 300.0, pw, 22.0, 0.20, 0.16, 0.18, 1.0);
    gfx.drawSolidRect(600.0, 300.0, pw * (game.enemy.hp / game.enemy.maxHp), 22.0, 0.90, 0.32, 0.30, 1.0);

    gfx.drawSolidRect(140.0, 250.0, 70.0, 90.0, 0.28, 0.52, 0.85, 1.0);
    gfx.drawSolidRect(760.0, 250.0, 70.0, 90.0, 0.82, 0.28, 0.26, 1.0);

    ui.beginFrameAndRender();
};
