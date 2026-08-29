

function basic() {
    print("start basic test");
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
    p.setIdentity("mygame", true);
    p.setupWriteDirectory();
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
    p.setIdentity("mygame", true);
    p.setupWriteDirectory();
    p.setSource(".");
    return true;
}

function textRoundTrip() {
    local p = eve.Filesystem();
    p.setIdentity("ev_ut_filesystem_text", true);
    if (!p.setupWriteDirectory()) return false;
    local expected = "galgame-save-v1\nnode=choice\n好感=2";
    if (!p.writeText("slot1.sav", expected)) return false;
    return p.readText("slot1.sav") == expected;
}
