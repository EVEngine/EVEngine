// Virtual-geometry (Nanite-style) demo.
//
// Preprocesses a procedural icosphere into a hierarchical cluster DAG, then each
// frame runs GPU-driven cluster culling (frustum + screen-space-error LOD) and a
// software rasterizer into a visibility buffer. The module returns the resolved
// RGBA as a ByteData plus per-frame stats (visible clusters / LOD levels).
//
// See docs/dev/VIRTUAL_GEOMETRY.md for the architecture.
//
// NOTE: the module is Vulkan-only. The RGBA produced by resolve() can be drawn
// with the host's image path, e.g. wrap it in an eve::image::ImageData and pass
// it to Graphics::newTextureFromImageData, then draw that texture as a quad.

local vg = eve.VirtualGeometry();
if (!vg.isAvailable()) {
    print("VirtualGeometry: requires the Vulkan backend\n");
    return;
}

local renderer = vg.newRenderer();
print("renderer ready: " + renderer.isReady() + "\n");

// ~5k-triangle unit icosphere -> cluster DAG (several LOD levels).
if (renderer.buildIcosphere(4))
    print("build ok: clusters=" + renderer.getClusterCount()
          + " tris=" + renderer.getTotalTriangleCount()
          + " maxLOD=" + renderer.getMaxLodLevel() + "\n");

renderer.setViewport(512, 512, 60.0, 1.0);   // error threshold: 1px

local camDist = 3.2;
local yaw = 0.0;

// Called each frame from the host loop.
function vg_step() {
    yaw += 0.012;
    renderer.setCameraSimple(0.0, 0.0, camDist, 0.1, 100.0);
    renderer.setModelYaw(yaw);

    // GPU-driven culling + software rasterization into the visibility buffer.
    local visible = renderer.update();

    // Resolve to RGBA (w*h*4 bytes) for display by the host.
    local rgba = renderer.resolve();
    if (rgba != null) {
        print("visible clusters=" + visible + " / " + renderer.getClusterCount()
              + " LODmax=" + renderer.getMaxLodLevel()
              + " view=" + renderer.getViewWidth() + "x" + renderer.getViewHeight()
              + " rgbaBytes=" + rgba.getSize() + "\n");
    }
}
