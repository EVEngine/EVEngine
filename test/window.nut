
function basicWindow() {
    local w = eve.Window();
    local ok = w.setWindowSettings(eve.WindowSettings());
    if (ok == false) return false;
    for (local i = 0; i <10000000; ++i) {local j = i;}
    w.close();
    return true;
}

function settingsFields() {
    local s = eve.WindowSettings();
    s.width = 640;
    s.height = 480;
    s.centered = true;
    if (s.width != 640) return false;
    if (s.height != 480) return false;
    if (s.centered != true) return false;
    return true;
}