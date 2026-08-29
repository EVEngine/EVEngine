// EVEngine <-> Blender model converter demo.
//
// Loads the native `modelconverter` plugin, asks it to convert a primitive box
// into a stone / stone wall using Blender's Python package (bpy), then reloads
// the produced .glb back into EVEngine with model3d to prove the pipeline.

persist mc = null
persist md = null
persist seed = 1847
persist converter = "stone"
persist converterNames = []
persist converterIndex = 0
persist status = "loading"
persist uiReady = false
persist autoRun = true
persist ran = false

function repoRoot() { return "../.."; }
function converterDir() { return repoRoot() + "/modelconverter/converters"; }
function pythonRuntimeDir() { return repoRoot() + "/modelconverter/python"; }
function boxModel() { return "box.obj"; }
function outModel(ext) { return "out/result." + ext; }

function runConversion() {
    status = "running…";
    if (mc == null) { status = "ModelConverter plugin not loaded"; return; }
    if (!mc.hasConverter(converter)) {
        status = "converter '" + converter + "' not found: " + mc.lastError();
        return;
    }
    local p = mc.newParams();
    p.setInt("seed", seed);
    p.setFloat("size", 1.25);
    p.setFloat("detail", 0.6);
    p.setFloat("length", 3.2);
    p.setFloat("height", 2.0);
    p.setFloat("depth", 0.6);
    if (!mc.convert(converter, boxModel(), outModel("glb"), "glb", p)) {
        status = "convert failed: " + mc.lastError();
        return;
    }
    // Reload the produced model into EVEngine to confirm it decodes.
    try {
        md = model3d.newModelDataFromFile(outModel("glb"));
        status = converter + " OK — " + md.getVertexCount() + " verts / "
                 + md.getFaceCount() + " faces (from " + md.getMeshCount() + " mesh)";
    } catch (e) {
        status = "converted but reload failed: " + e;
    }
}

function initConverter() {
    local plugins = eve.Plugins();
    if (!plugins.isLoaded(modelconverter_plugin)) {
        try {
            plugins.load(modelconverter_plugin);
        } catch (e) {
            status = "plugin load failed: " + e;
            return;
        }
    }
    mc = eve.ModelConverter();
    mc.configure(converterDir(), modelconverter_python, pythonRuntimeDir());
    converterNames = [];
    local n = mc.getConverterCount();
    for (local i = 0; i < n; i++) converterNames.append(mc.getConverterId(i));
    if (converterNames.len() == 0) {
        status = "no converters found under " + converterDir();
        return;
    }
    if (converterIndex >= converterNames.len()) converterIndex = 0;
    converter = converterNames[converterIndex];
    status = "ready — converters: " + converterNames.join(", ");
    if (autoRun) runConversion();
}

function buildPanel() {
    ui.setTheme("dark");
    ui.setNavKeyboard(true);
    ui.beginBuild();
    ui.beginWindow("MODEL CONVERTER / EVEngine ↔ Blender", "root");
    ui.text("Primitive box in → rich mesh out (via bpy)", "eyebrow");
    ui.text("Converter", "shapeLabel");
    ui.text("seed: " + seed, "sample");
    ui.button("Next converter", "nextConverter");
    ui.button("Re-convert (new seed)", "randomize");
    ui.button("Re-convert (same seed)", "rebuild");
    ui.text("", "status");
    ui.text("Requires bpy: pip install bpy", "hint");
    ui.end();
    ui.mountBuildAs("mc");
    ui.select("mc");
    ui.setHostOverlay(true);
    ui.setHostPos(760.0, 30.0, 300.0, 420.0);
    uiReady = true;
}

eve_init = function() {
    gfx.setBackgroundColor(0.05, 0.05, 0.06, 1.0);
    initConverter();
    if (!uiReady) buildPanel();
};

eve_update = function(dt) {
    local clicked = ui.consumeClick();
    while (clicked != "") {
        if (clicked == "mc/nextConverter") {
            converterIndex = (converterIndex + 1) % converterNames.len();
            converter = converterNames[converterIndex];
            runConversion();
        } else if (clicked == "mc/randomize") {
            seed = (seed * 1664525 + 1013904223) & 0x7fffffff;
            runConversion();
        } else if (clicked == "mc/rebuild") {
            runConversion();
        }
        clicked = ui.consumeClick();
    }
    if (uiReady) {
        ui.select("mc");
        ui.setText("status", status);
        ui.setText("sample", "seed: " + seed + "  →  " + converter);
    }
};

eve_render = function() {
    gfx.clear();
    gfx.render3D();
    ui.beginFrameAndRender();
};
