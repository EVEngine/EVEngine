// End-to-end Sprite2D test: independent PNG sequence -> runtime atlas ->
// per-instance playback speed/direction -> procedural transform -> ECS render.

local sheet = null;
local clip = null;
local actors = [];
local elapsed = 0.0;
local captured = false;

function make_actor(x, y, speed, scale, phase, reverse) {
    local quad = gfx.newQuad(0, 0, 128, 128);
    local player = anim.newSpriteAnim();
    player.setSheet(sheet);
    player.bindQuad(quad);
    player.setSpeed(speed);
    if (reverse) player.playReverse(clip);
    else player.play(clip);

    local sprite = gfx.newSprite2D();
    sprite.setTexture(sheet.getTexture());
    sprite.setQuad(quad);
    sprite.setSize(128.0, 128.0);
    sprite.setPosition(x, y);
    sprite.setScale(scale, scale);
    // Alpha avoids bright fringes from this asset's transparent-white RGB pixels.
    // Switch to "additive" after preprocessing the source to transparent-black.
    sprite.setBlend("alpha");
    sprite.setReceiveLight(false);
    player.bindSprite(sprite);

    local actor = {
        sprite = sprite,
        player = player,
        baseX = x,
        baseY = y,
        scale = scale,
        phase = phase,
        reverse = reverse
    };
    actors.push(actor);
}

eve_init = function() {
    gfx.setBackgroundColor(0.018, 0.024, 0.045, 1.0);

    // `{n}` is expanded from first..last. The loader validates RGBA8 and equal sizes,
    // packs a near-square atlas, uploads one texture, and records one Quad per frame.
    sheet = anim.newSpriteSheetFromSequence(gfx, "assets/frame_{n}.png", 1, 64, 8);
    clip = anim.newSpriteClip("gold-ring-burst");
    clip.addRange(0, sheet.getFrameCount() - 1, 24.0);
    clip.setLoop(true);

    make_actor(100.0, 120.0, 0.5, 1.25, 0.0, false);
    make_actor(410.0, 105.0, 1.0, 1.55, 1.4, false);
    make_actor(735.0, 95.0, 2.0, 1.8, 2.8, false);
    make_actor(455.0, 385.0, 1.0, 1.5, 4.2, true);

    // First actor accelerates, eases down, then accelerates again over a 4 s loop.
    actors[0].player.addSpeedCurveKey(0.0, 0.2);
    actors[0].player.addSpeedCurveKey(1.2, 2.4);
    actors[0].player.addSpeedCurveKey(2.6, 0.35);
    actors[0].player.addSpeedCurveKey(4.0, 0.2);
    actors[0].player.setSpeedCurveLoop(true);
    actors[0].player.setSpeedCurveInterpolation("cubic");
    clip.addEvent(18, "impact");

    // Exercise production sprite controls: custom pivot and UV mirroring.
    actors[2].sprite.setAnchor(0.25, 0.75);
    actors[3].sprite.setFlip(true, false);

    print("sprite-animation-vfx: curve speed, 1x/2x/reverse, pivot and flip active\n");
    print("sequence cache: " + anim.getSpriteSequenceCacheCount() + " atlas, " +
          anim.getSpriteSequenceCacheBytes() + " bytes\n");
};

eve_update = function(dt) {
    elapsed += dt;
    anim.update(dt);

    foreach (i, actor in actors) {
        local t = elapsed + actor.phase;
        local x = actor.baseX + sin(t * (0.7 + i * 0.12)) * (42.0 + i * 10.0);
        local y = actor.baseY + cos(t * (0.9 + i * 0.08)) * 34.0;
        local pulse = actor.scale * (1.0 + sin(t * 1.8) * 0.14);
        actor.sprite.setPosition(x, y);
        actor.sprite.setScale(pulse, pulse);
        actor.sprite.setRotation((t * (25.0 + i * 18.0)) % 360.0);

        local loops = actor.player.consumeLooped();
        if (loops > 0)
            print("actor " + i + " crossed " + loops + " loop(s), total=" + actor.player.getLoopCount() + "\n");
        local eventName = actor.player.consumeEvent();
        if (eventName != "") print("actor " + i + " event=" + eventName + "\n");
    }

    // saveFramePng reads the previously presented frame.
    if (!captured && elapsed > 1.5) {
        if (gfx.saveFramePng("sprite-animation-vfx.png")) {
            captured = true;
            print("sprite-animation-vfx: screenshot saved\n");
        }
    }
};

eve_render = function() {
    gfx.clear();

    // Reference lanes make translation/scale/rotation visible without font assets.
    gfx.drawSolidRect(40.0, 330.0, 1020.0, 2.0, 0.16, 0.22, 0.34, 1.0);
    gfx.drawSolidRect(40.0, 600.0, 1020.0, 2.0, 0.16, 0.22, 0.34, 1.0);
    gfx.renderSprites();
};
