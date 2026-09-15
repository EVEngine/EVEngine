local definition = checked(fluids.volumeDefaults());
definition.settings.gravity = [0.0, -10.0, 0.0];
definition.settings.capacity = 2;
local particle = clone checked(fluids.volumeEmissionDefaults()).description.prototype;
particle.position = [-0.2, 1.0, 0.0];
definition.particles = [particle];
local localSolver = checked(fluids.newVolumeSimulator(definition));
local target = checked(fluids.volumeColliderDefaults());
target.label = 72; target.center = [0.0, 1.0, 0.0]; target.radius = 0.1;
checked(localSolver.setColliders([target]));
if (checked(localSolver.bindDynamicParticles([0], 72, 0.0, 1000000000000.0, false)) != 1)
    throw "Dynamic particle attachment was not installed";
target.center = [0.25, 1.0, 0.0];
checked(localSolver.setColliders([target]));
checked(localSolver.step(1.0 / 120.0, 1));
local attached = checked(localSolver.snapshot());
if (attached.version != 20 || attached.attachments.len() != 1 ||
    !attached.attachments[0].dynamic || attached.attachments[0].compliance != 0.0 ||
    fabs(attached.particles[0].position[0] - 0.05) > 0.00001 ||
    attached.particles[0].material.phase != 0)
    throw "Dynamic particle attachment state mismatch";
if (checked(localSolver.unbindStaticParticles([0])) != 1)
    throw "Dynamic particle attachment was not removed";
return "VOLUME_DYNAMIC_ATTACHMENT_PASS";
