// WebGPU shell root script. When no main.nut ships with the game this runs the
// embedded demo (eve.demoScript) exactly like the desktop/iOS shells, but also
// exercises the WebGPU surface + ImGui overlay so a browser smoke test has
// something visible to validate.

// demoScript is injected by the engine at runtime (src/scripts/demo.nut).
if ("demoScript" in eve && eve.demoScript != null && eve.demoScript != "") {
    try {
        compilestring(eve.demoScript)();
    } catch (e) {
        print("embedded demo failed to load: " + e + "\n");
    }
}
