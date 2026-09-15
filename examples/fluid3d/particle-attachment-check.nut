local definition = checked(fluids.volumeDefaults());
definition.settings.gravity = [0.0, -10.0, 0.0];
definition.settings.capacity = 4;
local prototype = checked(fluids.volumeEmissionDefaults()).description.prototype;
local a = clone prototype; a.position = [-0.2, 1.0, 0.0];
local b = clone prototype; b.position = [0.2, 1.0, 0.0];
definition.particles = [a, b];
local localSolver = checked(fluids.newVolumeSimulator(definition));
local target = checked(fluids.volumeColliderDefaults());
target.label = 71; target.center = [0.0, 1.0, 0.0]; target.radius = 0.1;
checked(localSolver.setColliders([target]));
if (checked(localSolver.bindStaticParticles([0], 71, false)) != 1)
    throw "Static particle attachment was not installed";
target.center = [0.25, 1.2, 0.0];
checked(localSolver.setColliders([target]));
checked(localSolver.step(1.0 / 120.0, 1));
local attached = checked(localSolver.snapshot());
if (attached.version != 20 || attached.attachments.len() != 1 ||
    attached.attachments[0].constrainOrientation || attached.particles[0].material.phase != 0 ||
    fabs(attached.particles[0].position[0] - 0.05) > 0.00001 ||
    fabs(attached.particles[0].position[1] - 1.2) > 0.00001)
    throw "Static particle attachment state mismatch";
if (checked(localSolver.unbindStaticParticles([0])) != 1)
    throw "Static particle attachment was not removed";
return "VOLUME_PARTICLE_ATTACHMENT_PASS";
