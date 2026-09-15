persist cullingScene = null
persist cullingCamera = null
persist cullingObjects = []

function makeBlock(mesh, r, g, b) {
    local object = eve.Renderable3D()
    object.setMesh(mesh)
    object.setTint(r, g, b, 1.0)
    object.setRoughness(0.78)
    object.setCastShadow(true)
    cullingObjects.push(object)
    return object
}

function configureBoundedNode(id, tag) {
    local ref = cullingScene.getNodeRef(id)
    if (ref == null) throw "missing scene node: " + id
    ref.setBounds(-0.5, -0.5, -0.5, 0.5, 0.5, 0.5)
    if (tag != "") ref.addTag(tag)
}

eve_init = function() {
    gfx.setBackgroundColor(0.025, 0.055, 0.085, 1.0)
    gfx.setDirectionalLight(-0.45, -1.0, -0.25, 2.4, 2.25, 1.9)

    cullingCamera = eve.Camera3D()
    cullingCamera.setEye(0.0, 6.0, 14.0)
    cullingCamera.setTarget(0.0, 0.5, -12.0)
    cullingCamera.setClipPlanes(0.1, 100.0)
    cullingCamera.setAmbient(0.23, 0.27, 0.34)
    cullingCamera.setActive(true)

    cullingScene = eve.Scene()
    cullingScene.beginBuild()
    cullingScene.beginNode("root", "Pcg terrain roots")
    cullingScene.setBuildSpace("3d")
    cullingScene.beginNode("terrain-visible", "Visible terrain tile")
    cullingScene.setBuildPosition(0.0, -1.0, -12.0)
    cullingScene.setBuildScale(18.0, 0.8, 18.0)
    cullingScene.end()
    cullingScene.beginNode("terrain-culled", "Off-frustum terrain tile")
    cullingScene.setBuildPosition(48.0, 1.0, -14.0)
    cullingScene.setBuildScale(5.0, 5.0, 5.0)
    cullingScene.end()
    cullingScene.beginNode("landmark-control", "Untagged landmark")
    cullingScene.setBuildPosition(4.5, 1.4, -10.0)
    cullingScene.setBuildScale(2.0, 4.8, 2.0)
    cullingScene.end()
    cullingScene.end()
    if (!cullingScene.mountBuildAs("pcg-scene-player")) throw "scene mount failed"

    configureBoundedNode("terrain-visible", "terrain")
    configureBoundedNode("terrain-culled", "terrain")
    configureBoundedNode("landmark-control", "")

    local cube = gfx.newMeshCube(1.0)
    local terrain = makeBlock(cube, 0.16, 0.52, 0.25)
    local outside = makeBlock(cube, 0.95, 0.08, 0.15)
    local landmark = makeBlock(cube, 0.92, 0.66, 0.18)
    if (!cullingScene.linkRenderable3D("terrain-visible", terrain)) throw "terrain link failed"
    if (!cullingScene.linkRenderable3D("terrain-culled", outside)) throw "outside link failed"
    if (!cullingScene.linkRenderable3D("landmark-control", landmark)) throw "landmark link failed"

    cullingScene.updateTransforms()
    local result = cullingScene.applyPcgTerrainCullingAt(
        "pcg-scene-player", cullingCamera, 960.0, 640.0, "terrain")
    if (!result.ok) throw result.status.summary
    local changed = result.value
    cullingScene.updateTransforms()
    print("PCG_SCENE_PLAYER_CULLING_READY changed=" + changed +
          " visible=" + cullingScene.getNodeVisible("terrain-visible") +
          " culled=" + cullingScene.getNodeVisible("terrain-culled") +
          " control=" + cullingScene.getNodeVisible("landmark-control") + "\n")
}

eve_update = function(dt) {
    cullingScene.updateTransforms()
}

eve_render = function() {
    gfx.clear()
    gfx.render3D()
}
