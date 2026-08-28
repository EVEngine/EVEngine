

function basic() {
    print("start basic test of Graphics");
    if (eve.Graphics == null) {
        print("can not find filesystem");
        return false;
    }

    local p = eve.Graphics();
    if (p.getName() != "Graphics") {
        print("Graphics name is not right: ");
        print(p.getName());
        return false;
    }

    return true;
}

function hasDrawText() {
    local p = eve.Graphics();
    return ("drawText" in p) && ("print" in p);
}
