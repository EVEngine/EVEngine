dofile("map-data.nut");

actors <- [];
terrain <- [];
houseParts <- [];
houseModel <- null;
layers <- [];
camera <- null;
sun <- null;
selected <- 0;
riversideTime <- 0.0;
accumulator <- 0.0;
cameraYaw <- 0.0;

function validateRiversideData() {
    local fields = ["schema", "version", "unknownFields", "width", "height", "tileSize", "layers", "spawns", "blockedGids", "raisedHeight"];
    foreach (key, value in riversideData) if (fields.find(key) == null) throw "Unknown map field: " + key;
    foreach (key in fields) if (!(key in riversideData)) throw "Missing map field: " + key;
    if (riversideData.schema != "evengine.hd2d.riverside" || riversideData.version != 1)
        throw "Unsupported riverside map schema/version";
    if (riversideData.unknownFields != "reject" || riversideData.tileSize != 32 || riversideData.raisedHeight != 40)
        throw "Unsupported riverside map configuration";
    if (typeof riversideData.width != "integer" || typeof riversideData.height != "integer" ||
        riversideData.width <= 0 || riversideData.height <= 0 || riversideData.width > 256 || riversideData.height > 256)
        throw "Invalid riverside map dimensions";
    foreach (name, cells in riversideData.layers) {
        if (name != "ground" && name != "raised") throw "Unknown map layer";
        if (typeof cells != "array" || cells.len() != riversideData.width*riversideData.height)
            throw "Invalid map cell count";
        foreach (gid in cells) if (typeof gid != "integer" || gid < 0 || gid > 16) throw "Invalid tile GID";
    }
    if (!("ground" in riversideData.layers) || !("raised" in riversideData.layers)) throw "Missing map layer";
    if (riversideData.spawns.len() != 3) throw "Expected three character spawns";
    local identities = [];
    foreach (spawn in riversideData.spawns) {
        foreach (key,value in spawn) if (key != "id" && key != "x" && key != "y") throw "Unknown spawn field";
        if (["hero","scholar","guard"].find(spawn.id) == null || identities.find(spawn.id) != null)
            throw "Invalid character identity";
        identities.append(spawn.id);
        if (typeof spawn.x != "integer" || typeof spawn.y != "integer" ||
            blocked(spawn.x*32.0+16.0,spawn.y*32.0+16.0)) throw "Invalid or blocked character spawn";
    }
}

function blocked(x, z) {
    local radius = 6.0;
    foreach (dx in [-radius, radius]) foreach (dz in [-radius, radius]) {
        local tx = floor((x + dx) / 32.0).tointeger();
        local ty = floor((z + dz) / 32.0).tointeger();
        if (tx < 0 || ty < 0 || tx >= riversideData.width || ty >= riversideData.height) return true;
        local i = ty * riversideData.width + tx;
        if (riversideData.layers.raised[i] != 0) return true;
        if (riversideData.blockedGids.find(riversideData.layers.ground[i]) != null) return true;
    }
    return false;
}

function moveActor(a, dx, dz, dt) {
    local oldX = a.x, oldZ = a.z;
    if (!blocked(a.x + dx * dt, a.z)) a.x += dx * dt;
    if (!blocked(a.x, a.z + dz * dt)) a.z += dz * dt;
    local moving = a.x != oldX || a.z != oldZ;
    local direction = a.direction;
    if (moving) {
        // Sheet rows are south, east, north, west; movement stays four-way.
        if (dx > 0) direction = 1;
        else if (dx < 0) direction = 3;
        else if (dz > 0) direction = 0;
        else direction = 2;
    }
    if (moving && (!a.moving || direction != a.direction)) a.sprite.play(direction * 6, direction * 6 + 5, 9.0);
    if (!moving && a.moving) { a.sprite.stop(); a.sprite.setFrameIndex(direction * 6 + 2); }
    a.direction = direction;
    a.moving = moving;
    // Atlas feet sit 4 pixels above its bottom; align the opaque feet with the ground.
    a.sprite.setPosition(a.x, 0.0, a.z);
    a.sprite.update(dt);
}

function stepScene(dt) {
    riversideTime += dt;
    foreach (i, a in actors) {
        local dx = 0.0, dz = 0.0;
        if (i == selected) {
            if (keyboard.isDown("a") || keyboard.isDown("Left")) dx = -70.0;
            else if (keyboard.isDown("d") || keyboard.isDown("Right")) dx = 70.0;
            else if (keyboard.isDown("w") || keyboard.isDown("Up")) dz = -70.0;
            else if (keyboard.isDown("s") || keyboard.isDown("Down")) dz = 70.0;
        } else {
            local phase = (floor(riversideTime / 1.2).tointeger() + i) % 4;
            if (phase == 0) dx = 22.0;
            if (phase == 1) dz = 22.0;
            if (phase == 2) dx = -22.0;
            if (phase == 3) dz = -22.0;
        }
        moveActor(a, dx, dz, dt);
    }
}

eve_init = function() {
    validateRiversideData();
    gfx.setBackgroundColor(0.065, 0.095, 0.13, 1.0);
    local atlas = gfx.newTextureFromFile("assets/terrain.png");
    foreach (name in ["ground", "raised"]) {
        local layer = map.newLayer(riversideData.width, riversideData.height, 32.0, 32.0);
        layer.setVisible(false);
        layer.setTileset(atlas, 1, 4, 0, 0);
        layer.setTilesetTileSize(64, 64);
        for (local y=0; y<riversideData.height; ++y) for (local x=0; x<riversideData.width; ++x) {
            local gid = riversideData.layers[name][y*riversideData.width+x];
            // GID 10 remains the authoritative building footprint for collision;
            // an imported OBJ provides its visible geometry.
            layer.setTile(x, y, name == "raised" && gid == 10 ? 0 : gid);
        }
        local builder = hd2d.newTileMap3D();
        builder.setSideDepth(name == "ground" ? 14.0 : 40.0);
        builder.setWallUV(0.25, 0.5, 0.5, 0.75);
        if (name == "raised") {
            layer.setTileDataNumber(9, "height", 40.0);
            layer.setTileDataNumber(10, "height", 40.0);
        }
        local surface = builder.buildRenderable(gfx, layer);
        surface.setRoughness(1.0);
        terrain.append(surface);
        layers.append(layer);
    }
    camera = eve.Camera3D();
    camera.setTarget(352.0, 0.0, 256.0);
    camera.setEye(352.0, 560.0, 960.0);
    camera.setFov(43.0);
    camera.setClipPlanes(1.0, 3000.0);
    camera.setActive(true);
    camera.setAmbient(0.25, 0.28, 0.34);
    sun = eve.Light3D();
    sun.setType("dir");
    sun.setDirection(-0.6, 1.0, 0.5);
    sun.setColor(1.0, 0.93, 0.8, 0.8);
    sun.setCastShadow(true);
    sun.setShadowStrength(0.7);
    local hx0=riversideData.width, hz0=riversideData.height, hx1=-1, hz1=-1;
    for (local z=0;z<riversideData.height;++z) for (local x=0;x<riversideData.width;++x)
        if (riversideData.layers.raised[z*riversideData.width+x] == 10) {
            if (x<hx0) hx0=x; if (x>hx1) hx1=x;
            if (z<hz0) hz0=z; if (z>hz1) hz1=z;
        }
    if (hx1 < hx0) throw "Missing house footprint";
    houseModel = model3d.newModelDataFromFile("assets/house/cottage.obj");
    if (houseModel.getMeshCount() == 0) throw "Empty cottage OBJ";
    for (local mesh=0;mesh<houseModel.getMeshCount();++mesh) {
        local part = model3d.createRenderable(gfx,houseModel,mesh);
        part.setPosition((hx0+hx1+1)*16.0,0.0,(hz0+hz1+1)*16.0);
        part.setRoughness(0.95);
        part.setCastShadow(true);
        part.setReceiveShadow(true);
        houseParts.append(part);
    }
    foreach (spawn in riversideData.spawns) {
        local sprite = hd2d.newSprite(gfx);
        sprite.setTexture(gfx.newTextureFromFile("assets/" + spawn.id + "/64/final/walk-sheet-clean.png"));
        gfx.setTextureSampler(sprite.getTexture(), "nearest", "none", 1.0, 0.0);
        sprite.setFrameGrid(6, 4);
        sprite.setSize(64.0, 64.0);
        sprite.setPivot(0.5, 60.0/64.0);
        sprite.setCamera(camera);
        local a = {id=spawn.id, sprite=sprite, x=spawn.x*32.0+16.0, z=spawn.y*32.0+16.0, direction=0, moving=false};
        actors.append(a);
        moveActor(a, 0.0, 0.0, 0.0);
    }
    print("HD2D_RIVERSIDE_READY actors=3 directions=4 tileLayers=2\n");
};

eve_update = function(dt) {
    if (keyboard.isDown("1")) selected=0;
    if (keyboard.isDown("2")) selected=1;
    if (keyboard.isDown("3")) selected=2;
    if (keyboard.isDown("q")) cameraYaw -= dt*0.5;
    if (keyboard.isDown("e")) cameraYaw += dt*0.5;
    camera.setEye(352.0+sin(cameraYaw)*704.0, 560.0, 256.0+cos(cameraYaw)*704.0);
    accumulator += dt < 0.25 ? dt : 0.25;
    while (accumulator >= 1.0/60.0) { stepScene(1.0/60.0); accumulator -= 1.0/60.0; }
};

eve_render = function() { gfx.clear(); gfx.render3D(); };
