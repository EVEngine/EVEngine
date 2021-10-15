

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

function getPaths() {
    local p = eve.Filesystem();
    print("getWorkingDirectory: "+p.getWorkingDirectory());
    print("getUserDirectory: "+p.getUserDirectory());
    print("getAppdataDirectory: "+p.getAppdataDirectory());
    print("getSaveDirectory: "+p.getSaveDirectory());
    print("getSourceBaseDirectory: "+p.getSourceBaseDirectory());
    return true;
}


function readDir() {
    local p = eve.Filesystem();
    if (p.getName() != "Filesystem") {
        print("filesystem name is not right: ");
        print(p.getName());
        return false;
    }
    return true;
}