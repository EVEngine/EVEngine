local definition = checked(fluids.volumeDefaults());
definition.settings.capacity = 2;
local prototype = checked(fluids.volumeEmissionDefaults()).description.prototype;
local a = clone prototype; a.position = [0.5, 1.0, 0.5]; a.actorGroup = 7;
a.orientation = [0.0, 0.0, 0.70710678, 0.70710678];
local b = clone prototype; b.position = [-0.5, 1.0, 0.0]; b.actorGroup = 8;
definition.particles = [a, b];
local localSolver = checked(fluids.newVolumeSimulator(definition));
local frames = checked(localSolver.debugParticleFrames(7, 2.0, 1));
if (frames.len() != 1 || frames[0].particleIndex != 0 ||
    fabs(frames[0].x[0] - 0.5) > 0.00001 || fabs(frames[0].x[1] - 3.0) > 0.00001 ||
    fabs(frames[0].y[0] + 1.5) > 0.00001 || fabs(frames[0].z[2] - 2.5) > 0.00001)
    throw "Particle frame debug axes mismatch";
return "VOLUME_PARTICLE_FRAME_PASS";
