
function basic() {
    local t = eve.Timer();
    if (t.getName() != "Timer") return false;
    t.step();
    local d = t.getDelta();
    return d >= 0.0;
}
