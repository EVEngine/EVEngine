// Built-in profiler script API smoke test.
function captureWorks() {
    local p = eve.Profiler();
    p.setEnabled(true);
    p.beginFrame();
    p.begin("scriptScope");
    for (local i = 0; i < 1000; ++i) { /* busy */ }
    p.end();
    p.endFrame();
    local rows = p.capture();
    if (typeof rows != "array") return false;
    local found = false;
    foreach (r in rows) {
        if (typeof r != "table") return false;
        if (!("name" in r) || !("selfMs" in r) || !("totalMs" in r)) return false;
        if (r.name == "scriptScope") found = true;
    }
    if (!found) return false;
    local tops = p.hotspots(3, 0.0);
    if (typeof tops != "array" || tops.len() < 1) return false;
    if (tops[0].name != "scriptScope") return false;
    if (p.frameMs() < 0.0) return false;
    if (p.textReport().find("scriptScope") == null) return false;
    p.setEnabled(false);
    return true;
}
