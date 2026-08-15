// EVEngine WebGPU minimal demo: bouncing 2D squares + HUD.
// Uses only the trimmed module set (Window/Graphics/Timer).

eve_init <- function() {
    gfx.setBackgroundColor(0.04, 0.05, 0.12, 1.0);
    x <- 0.0;
    y <- 0.0;
    vx <- 180.0;
    vy <- 120.0;
    frameCount <- 0;
    print("eve_init ok\n");
};

eve_update <- function(dt) {
    x = x + vx * dt;
    y = y + vy * dt;
    if (x < 0 || x > config.width - 40.0) vx = -vx;
    if (y < 0 || y > config.height - 40.0) vy = -vy;
};

eve_render <- function() {
    gfx.clear();

    // Bouncing square.
    gfx.drawSolidRect(x, y, 40.0, 40.0, 0.35, 0.85, 1.0, 1.0);

    // Static HUD bar.
    gfx.drawSolidRect(10.0, 10.0, 180.0, 28.0, 0.15, 0.5, 0.95, 0.9);

    frameCount += 1;
    if (frameCount % 30 == 0) {
        print("frame " + frameCount + "\n");
    }
};
