function basic() {
    if (eve.Joystick == null) {
        print("can not find Joystick");
        return false;
    }

    local p = eve.Joystick();
    if (p.getName() != "Joystick") {
        print("Joystick name is not right: ");
        print(p.getName());
        return false;
    }
    return true;
}

function padClass() {
    if (eve.Pad == null) {
        print("can not find Pad class");
        return false;
    }
    return true;
}

function query() {
    local joy = eve.Joystick();
    local n = joy.getJoystickCount();
    if (n < 0) return false;
    if (joy.getJoystick(-1) != null) return false;
    if (joy.getJoystick(n) != null) return false;
    if (joy.getJoystickFromID(-999999) != null) return false;

    for (local i = 0; i < n; i++) {
        local pad = joy.getJoystick(i);
        if (pad == null) return false;
        if (joy.getIndex(pad) != i) return false;
        if (pad.getAxisCount() < 0) return false;
        if (pad.getButtonCount() < 0) return false;
        if (pad.getHatCount() < 0) return false;
        if (pad.getID() < 0) return false;
    }
    return true;
}

function axes() {
    local joy = eve.Joystick();
    local n = joy.getJoystickCount();
    if (n == 0) return true;  // no device attached; nothing to verify

    local pad = joy.getJoystick(0);
    local axes = pad.getAxes();
    if (typeof axes != "array") return false;
    if (axes.len() != pad.getAxisCount()) return false;
    for (local i = 0; i < axes.len(); i++) {
        if (axes[i] < -1.0 || axes[i] > 1.0) return false;
    }
    if (pad.getHatCount() > 0 && pad.getHat(0) == "") return false;
    return true;
}
