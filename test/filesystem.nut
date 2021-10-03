

function basic() {
    if (eve.Filesystem == null) {
        print("can not find filesystem");
        return false;
    }

    local p = eve.Filesystem();
    if (p.getName() != "Filesystem") {
        print("filesystem name is not right: ");
        print(p.getName());
        return false;
    }
    return true;
}