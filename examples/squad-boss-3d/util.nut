// Shared helpers. No graphics, clock, or random sources.
function sbMin(a, b) { return a < b ? a : b; }
function sbMax(a, b) { return a > b ? a : b; }
function sbClamp(v, a, b) { return sbMin(b, sbMax(a, v)); }
function sbDistanceXZ(a, b) {
    local dx = a.x - b.x;
    local dz = a.z - b.z;
    return sqrt(dx * dx + dz * dz);
}
function sbRequire(result, what) {
    if (!result.ok) throw what + ": " + result.status.summary;
    return result.value;
}
function sbNote(sim, message) {
    sim.events.append({ time = sim.time, text = message });
    if (sim.events.len() > 6) sim.events.remove(0);
    print("squad-boss-3d: " + message + "\n");
}

// Arena extents in metres (XZ). Soft bounds for movement and EQS consumers.
SB_ARENA <- { minX = -7.5, maxX = 7.5, minZ = -5.5, maxZ = 5.5 };

function sbClampArena(x, z) {
    return {
        x = sbClamp(x, SB_ARENA.minX, SB_ARENA.maxX),
        z = sbClamp(z, SB_ARENA.minZ, SB_ARENA.maxZ)
    };
}
