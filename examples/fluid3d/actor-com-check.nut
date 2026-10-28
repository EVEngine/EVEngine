// Opt-in check for the package ActorCOMTransform mass-center dependency.
function verifyVolumeFluidActorCOM() {
    local definition = checked(fluids.volumeDefaults());
    definition.settings.spacing = 0.1;
    local first = clone checked(fluids.volumeEmissionDefaults()).description.prototype;
    first.position = [0.0, 1.0, 0.0];
    first.actorGroup = 7;
    first.material.density = 1000.0;
    local second = clone first;
    second.material = clone first.material;
    second.position = [1.0, 1.0, 0.0];
    second.material.density = 500.0;
    local excluded = clone first;
    excluded.position = [1.5, 1.0, 0.0];
    excluded.actorGroup = 8;
    definition.particles = [first, second, excluded];
    local source = checked(fluids.newVolumeSimulator(definition));
    local properties = checked(source.actorMassProperties(7));
    if (properties.particleCount != 2 || fabs(properties.mass - 1.5) > 0.00001)
        throw "Actor mass summary is incorrect";
    if (fabs(properties.centerOfMass[0] - 1.0 / 3.0) > 0.00001 ||
        fabs(properties.centerOfMass[1] - 1.0) > 0.00001)
        throw "Actor center of mass is incorrect";
    if (source.actorMassProperties(9).ok)
        throw "Missing actor group was accepted";
    return true;
}
