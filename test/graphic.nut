

function basic() {
    print("start basic test of graphic");
    if (eve.Graphic == null) {
        print("can not find filesystem");
        return false;
    }

    local p = eve.Graphic();
    if (p.getName() != "Graphics") {
        print("filesystem name is not right: ");
        print(p.getName());
        return false;
    }

    return true;
}
