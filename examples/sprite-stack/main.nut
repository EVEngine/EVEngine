// Classic SpriteStack rendered entirely by the normal 2D pipeline.
// A / D rotate, 1..4 switch primitive, Q / E change slice spacing.
persist angle = 0.0
persist kind = "cylinder"
persist spacing = 4.0
persist stack = null
persist batch = null
persist copies = []

local tints = {
    cylinder = [0.32, 0.82, 0.68], sphere = [0.92, 0.52, 0.26],
    cone = [0.68, 0.48, 0.30], box = [0.42, 0.62, 0.92]
};

function rebuild() {
    local layers = spritestack.slicePrimitive(kind, 20, 128, 128, "y", 0.0);
    stack = spritestack.newStack(gfx);
    stack.setLayerCount(layers.len());
    for (local i = 0; i < layers.len(); i++) stack.setLayerImage(gfx, layers[i], i);
    stack.setPosition(480.0, 390.0);
    stack.setSize(180.0, 180.0);
    stack.setThickness(spacing);
    local tint = tints[kind];
    stack.setTint(tint[0], tint[1], tint[2], 1.0);
    stack.setShadowEnabled(true);
    stack.setShadowOpacity(0.42);
    stack.setShadowOffset(12.0, 9.0);
    stack.setOutline(2.0, 0.02, 0.03, 0.04);

    batch = spritestack.newBatch(gfx);
    copies = [];
    foreach (x in [190.0, 770.0]) {
        local copy = spritestack.newStack(gfx);
        copy.setLayerCount(stack.getLayerCount());
        for (local i = 0; i < stack.getLayerCount(); i++)
            copy.setLayerTexture(stack.getLayerTexture(i), i);
        copy.setPosition(x, 360.0);
        copy.setSize(105.0, 105.0);
        copy.setThickness(spacing * 0.55);
        copy.setTint(tint[0], tint[1], tint[2], 1.0);
        copy.setShadowEnabled(true);
        batch.add(copy);
        copies.append(copy);
    }
}

gfx.setBackgroundColor(0.055, 0.075, 0.095, 1.0);
rebuild();

function eve_update(dt) {
    if (keyboard.isDown("a") || keyboard.isDown("A")) angle -= dt * 75.0;
    if (keyboard.isDown("d") || keyboard.isDown("D")) angle += dt * 75.0;
    if (key_just_pressed("1")) { kind = "cylinder"; rebuild(); }
    if (key_just_pressed("2")) { kind = "sphere"; rebuild(); }
    if (key_just_pressed("3")) { kind = "cone"; rebuild(); }
    if (key_just_pressed("4")) { kind = "box"; rebuild(); }
    if (key_just_pressed("q") || key_just_pressed("Q")) { spacing = max(1.0, spacing - 1.0); rebuild(); }
    if (key_just_pressed("e") || key_just_pressed("E")) { spacing = min(10.0, spacing + 1.0); rebuild(); }
    stack.setRotation(angle);
    foreach (copy in copies) copy.setRotation(-angle * 0.7);
}

function eve_render() {
    gfx.clear();
    stack.render(gfx);
    batch.render(gfx);
}
