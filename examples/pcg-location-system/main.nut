persist pcgLocationCamera = null
persist pcgLocationObjects = []
persist pcgLocationProfile = null

function addLandmark(mesh, x, y, z, sx, sy, sz, r, g, b) {
    local object = eve.Renderable3D();
    object.setMesh(mesh); object.setPosition(x, y, z); object.setScale(sx, sy, sz);
    object.setTint(r, g, b, 1.0); object.setRoughness(0.7); object.setCastShadow(true);
    pcgLocationObjects.push(object);
}

eve_init = function() {
    gfx.setBackgroundColor(0.035, 0.065, 0.105, 1.0);
    pcgLocationCamera = eve.Camera3D();
    pcgLocationCamera.setTarget(0.0, 1.5, -10.0);
    pcgLocationCamera.setAmbient(0.28, 0.32, 0.38);
    pcgLocationCamera.setClipPlanes(0.1, 100.0);

    pcgLocationProfile = eve.LocationProfile();
    local overlook = eve.LocationPose(); overlook.x = 13.0; overlook.y = 8.0; overlook.z = 15.0;
    local player = eve.LocationPose(); player.x = 0.0; player.y = 1.0; player.z = -10.0;
    local bookmark = eve.LocationBookmark(); bookmark.name = "Overlook";
    bookmark.controller = "FlyingCamera"; bookmark.scene = "PcgLocationDemo";
    bookmark.setCamera(overlook); bookmark.setPlayer(player);
    assert(pcgLocationProfile.addBookmark(bookmark).ok);
    local loaded = pcgLocationProfile.loadBookmark(0);
    assert(loaded.ok && loaded.value.hasPlayer);
    pcgLocationCamera.setEye(loaded.value.camera.x, loaded.value.camera.y, loaded.value.camera.z);

    local cube = gfx.newMeshCube(1.0);
    addLandmark(cube, 0.0, -1.0, -10.0, 24.0, 0.5, 28.0, 0.10, 0.22, 0.14);
    addLandmark(cube, -5.0, 1.5, -10.0, 2.4, 5.0, 2.4, 0.20, 0.75, 0.35);
    addLandmark(cube, 0.0, 2.5, -10.0, 3.0, 7.0, 3.0, 0.20, 0.48, 0.95);
    addLandmark(cube, 5.0, 1.0, -10.0, 2.0, 4.0, 2.0, 0.95, 0.48, 0.16);
    print("PCG_LOCATION_SYSTEM_READY bookmark=Overlook controller=" +
          pcgLocationProfile.getBookmarkController(0).value + " scene=" +
          pcgLocationProfile.getBookmarkScene(0).value + "\n");
};

eve_update = function(dt) {};
eve_render = function() { gfx.clear(); gfx.render3D(); };
