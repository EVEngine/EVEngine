
function basicWindow() {
    local w = eve.Window();
    local ok = w.setWindowSettings(eve.WindowSettings());
    if (ok == false) return false;
    for (local i = 0; i <10000000; ++i) {local j = i;}
    w.close();
    return true;
}