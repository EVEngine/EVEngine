function basic() {
    if (eve.Decal == null) {
        print("decal module not built\n");
        return false;
    }
    local decal = eve.Decal();
    if (decal.getName() != "Decal") {
        print("Decal name mismatch\n");
        return false;
    }
    decal.clearAll();
    if (decal.count() != 0) {
        print("decal.clearAll did not clear\n");
        return false;
    }
    decal.setLimit("blood", 2);
    decal.update(0.016);
    return decal.count() == 0;
}
