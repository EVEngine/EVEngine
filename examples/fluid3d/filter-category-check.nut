// Opt-in check for the package SetCategory actor helper.
function verifyVolumeFluidFilterCategory() {
    local definition = checked(fluids.volumeDefaults());
    local first = clone checked(fluids.volumeEmissionDefaults()).description.prototype;
    first.position = [0.0, 1.0, 0.0];
    first.actorGroup = 7;
    first.collisionFilter = 15728641; // 0x00f00001
    local second = clone first;
    second.position = [0.2, 1.0, 0.0];
    second.actorGroup = 8;
    second.collisionFilter = 267386884; // 0x0ff00004
    definition.particles = [first, second];
    local source = checked(fluids.newVolumeSimulator(definition));
    checked(source.setActorFilterCategory(7, 3));
    local state = checked(source.snapshot());
    if (state.particles[0].collisionFilter != 15728648 ||
        state.particles[1].collisionFilter != 267386884)
        throw "Actor filter category or mask preservation is incorrect";
    if (source.setActorFilterCategory(7, 16).ok || source.setActorFilterCategory(9, 1).ok)
        throw "Invalid actor filter update was accepted";
    return true;
}
