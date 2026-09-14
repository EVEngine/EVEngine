local definition = checked(fluids.volumeDefaults());
definition.settings.gravity = [0.0, -10.0, 0.0];
definition.settings.iterations = 2;
definition.particles = [];
local solver = checked(fluids.newVolumeSimulator(definition));

local particle = checked(fluids.volumeEmissionDefaults()).description.prototype;
particle.position = [0.2, 0.5, 0.0];
checked(solver.emit([particle]));

local sphere = checked(fluids.volumeColliderDefaults());
sphere.label = 92;
sphere.center = [0.0, 0.5, 0.0];
sphere.radius = 0.5;
checked(solver.setColliders([sphere]));
checked(solver.step(1.0 / 120.0, 1));

local captured = checked(solver.grabContactParticles(92, 0.0, 0.5, 0.0,
                                                      0.0, 0.0, 0.0, 1.0, 0.01));
if (captured != 1) throw "contact grabber did not capture particle";
local before = checked(solver.snapshot()).particles[0].position;
local moved = checked(solver.updateGrabbedParticles(92, 0.3, 0.7, 0.0,
                                                    0.0, 0.0, 0.0, 1.0));
if (moved != 1) throw "contact grabber did not update particle";
checked(solver.step(1.0 / 120.0, 1));
local held = checked(solver.snapshot()).particles[0];
if (abs(held.position[0] - before[0] - 0.3) > 0.000001 ||
    abs(held.position[1] - before[1] - 0.2) > 0.000001)
    throw "contact grabber local pose was not preserved";
if (checked(solver.releaseGrabbedParticles(92)) != 1) throw "contact grabber release mismatch";
::contactGrabberPass <- true;
