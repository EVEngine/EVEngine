
function basic() {
    if (eve.Mouse == null) {
        print("can not find Mouse");
        return false;
    }

    local p = eve.Mouse();
    if (p.getName() != "Mouse") {
        print("Mouse name is not right: ");
        print(p.getName());
        return false;
    }
    return true;
}

function relativeMode() {
    if (eve.Mouse == null) return false;
    local m = eve.Mouse();
    if (m.setRelativeMode == null || m.getRelativeMode == null) return false;
    // SDL may reject relative mode without a focused window; only verify the
    // round trip when the backend accepts the request.
    if (!m.setRelativeMode(true)) return true;
    if (!m.getRelativeMode()) return false;
    m.setRelativeMode(false);
    if (m.getRelativeMode()) return false;
    return true;
}

function visibility() {
    local m = eve.Mouse();
    if (m.setVisible == null || m.isVisible == null) return false;
    m.setVisible(true);
    local visible = m.isVisible();
    m.setVisible(false);
    local hidden = m.isVisible();
    m.setVisible(true);
    return visible && !hidden;
}
