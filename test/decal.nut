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
    local missingProjection = decal.setProjection(99999, "spherical", 4.0);
    local missingWorldProjection = decal.setProjection(99999, "world", 4.0);
    local missingParallax = decal.setParallax(99999, 0.05, 8.0, 24.0);
    local missingEdgeFade = decal.setEdgeFade(99999, 0.08);
    if (missingProjection == null || missingWorldProjection == null || missingParallax == null || missingEdgeFade == null) return false;
    if (decal.proceduralPresets() != "blood-wet,blood-dried,damage,dirt,rust,puddle,paint,moss,mold,lichen") {
        print("Procedural Decal preset catalogue mismatch\n");
        return false;
    }
    return decal.count() == 0;
}
