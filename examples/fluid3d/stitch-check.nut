local definition = checked(fluids.volumeDefaults());
definition.settings.gravity = [0.0, 0.0, 0.0]; definition.settings.capacity = 2;
local prototype = checked(fluids.volumeEmissionDefaults()).description.prototype;
local a = clone prototype; a.material = clone prototype.material;
a.position = [-0.2, 1.0, 0.0]; a.material.density = 1000.0; a.selfCollide = false;
local b = clone prototype; b.material = clone prototype.material;
b.position = [0.2, 1.0, 0.0]; b.material.density = 2000.0; b.selfCollide = false;
definition.particles = [a, b];
local localSolver = checked(fluids.newVolumeSimulator(definition));
checked(localSolver.setStitches([{particleIndex1=0,particleIndex2=1,compliance=0.0}]));
checked(localSolver.step(1.0 / 120.0, 1));
local state = checked(localSolver.snapshot());
if (state.version != 20 || state.stitches.len() != 1 ||
    fabs(state.particles[0].position[0] - state.particles[1].position[0]) > 0.00001 ||
    fabs(state.particles[0].position[0] - 0.06666667) > 0.00001)
    throw format("Stitch mass weighting mismatch: %.7f %.7f", state.particles[0].position[0],
                 state.particles[1].position[0]);
checked(localSolver.killParticle(0));
if (checked(localSolver.snapshot()).stitches.len() != 0) throw "Dead-particle stitch survived";
return "VOLUME_STITCH_PASS";
