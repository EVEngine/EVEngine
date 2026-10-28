// Opt-in check for Fluid3DInstancedParticleRenderer-style actor instance data.
local definition = checked(fluids.volumeDefaults());
definition.settings.gravity = [0.0, 0.0, 0.0];
local prototype = checked(fluids.volumeEmissionDefaults()).description.prototype;
local first = clone prototype;
first.position = [0.0, 1.0, 0.0]; first.velocity = [1.0, 0.0, 0.0];
first.radii = [0.1, 0.2, 0.3]; first.actorGroup = 7; first.color = [0.2, 0.3, 0.4, 0.5];
local second = clone prototype;
second.position = [0.5, 1.0, 0.0]; second.actorGroup = 8;
definition.particles = [first, second];
local localSolver = checked(fluids.newVolumeSimulator(definition));
checked(localSolver.step(1.0 / 120.0, 1));
local instances = checked(localSolver.particleInstances(7, 2.0, 3.0, 4.0, 0.5, 1));
if (instances.len() != 1 || instances[0].particleIndex != 0 ||
    fabs(instances[0].position[0] - 1.0 / 240.0) > 0.00001 ||
    fabs(instances[0].scale[0] - 0.2) > 0.00001 ||
    fabs(instances[0].scale[1] - 0.6) > 0.00001 ||
    fabs(instances[0].scale[2] - 1.2) > 0.00001 ||
    fabs(instances[0].color[3] - 0.5) > 0.00001)
    throw "Particle instance data mismatch";
if (localSolver.particleInstances(7, 1.0, 1.0, 1.0, 1.0, 0).ok)
    throw "Particle instance budget was not enforced";
return "VOLUME_PARTICLE_INSTANCES_PASS";
