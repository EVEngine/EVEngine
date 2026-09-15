local definition = checked(fluids.volumeDefaults());
definition.settings.gravity = [0.0, 0.0, 0.0];
definition.settings.iterations = 1;
definition.particles = [];
local solver = checked(fluids.newVolumeSimulator(definition));

local particle = checked(fluids.volumeEmissionDefaults()).description.prototype;
particle.position = [0.56, 1.0, 0.0];
particle.material.stickiness = 0.0;
particle.material.stickDistance = 0.1;
particle.material.stickinessCombine = 1;
checked(solver.emit([particle]));

local sphere = checked(fluids.volumeColliderDefaults());
sphere.label = 91;
sphere.center = [0.0, 1.0, 0.0];
sphere.radius = 0.5;
sphere.stickiness = 10.0;
sphere.stickDistance = 0.1;
sphere.stickinessCombine = 3;
checked(solver.setColliders([sphere]));
checked(solver.step(1.0 / 120.0, 1));

local state = checked(solver.snapshot());
local contacts = checked(solver.contacts());
if (state.version != 20) throw "collision material snapshot version mismatch";
if (state.particles[0].position[0] >= 0.56) throw "maximum-priority adhesion did not pull particle";
if (contacts.len() != 1 || contacts[0].colliderLabel != 91) throw "adhesive contact mismatch";
::collisionMaterialPass <- true;
