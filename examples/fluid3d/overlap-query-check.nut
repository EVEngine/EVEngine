// Opt-in acceptance probe. Loading this file has no side effects.
function verifyVolumeFluidOverlapTriggers() {
    paused = true;
    rebuild(14);
    updateOverlapTriggers(0.0);
    if (overlapCounts.len() != 2 || overlapCounts[0] <= 0 || overlapCounts[1] <= 0)
        throw "Expected both overlap triggers to contain liquid";
    local state = checked(solver.snapshot());
    local red = 0, yellow = 0, cyan = 0;
    foreach (particle in state.particles) {
        if (particle.color[0] > 0.9 && particle.color[1] < 0.2) ++red;
        else if (particle.color[0] > 0.9 && particle.color[1] > 0.7) ++yellow;
        else if (particle.color[1] > 0.7 && particle.color[2] > 0.8) ++cyan;
    }
    if (red <= 0 || yellow <= 0 || cyan <= 0)
        throw "Overlap trigger colors were not observable";
    local before = state.particles[0].color;
    local bad = clone overlapQueries[0]; bad.rotation = [0.0, 0.0, 0.0, 2.0];
    if (solver.applyQueryColors([bad], [[1.0,0.0,0.0,1.0]], 0.0,1.0,1.0,1.0,1024).ok)
        throw "Invalid overlap trigger accepted";
    local after = checked(solver.snapshot()).particles[0].color;
    if (before[0] != after[0] || before[1] != after[1] || before[2] != after[2] || before[3] != after[3])
        throw "Failed overlap trigger changed particle colors";
    return "VOLUME_OVERLAP_TRIGGER_PASS red="+red+" yellow="+yellow+" cyan="+cyan;
}
