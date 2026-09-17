// Opt-in runtime check for Fluid3DEmitter AddShape/RemoveShape/UpdateEmitterDistribution parity.
local definition = checked(fluids.volumeDefaults());
definition.settings.capacity = 8;
definition.settings.gravity = [0.0, 0.0, 0.0];
local localSolver = checked(fluids.newVolumeSimulator(definition));
local emitterBase = checked(fluids.volumeEmissionDefaults());
emitterBase.description.speed = 0.0;
emitterBase.description.actorCapacity = 8;
emitterBase.description.prototype.color = [0.2, 0.3, 0.4, 1.0];
local left = checked(fluids.volumeEmissionFromEdge(0.0, 0.1, 0.0));
left.description.origin = [-0.25, 1.0, 0.0];
left.description.distribution[0].color = [1.0, 0.0, 0.0, 1.0];
local right = checked(fluids.volumeEmissionFromEdge(0.0, 0.1, 0.0));
right.description.origin = [0.25, 1.0, 0.0];
right.description.distribution[0].color = [0.0, 0.0, 1.0, 1.0];
local combined = checked(fluids.composeVolumeEmitterShapes(emitterBase, [left, right]));
if (combined.description.distribution.len() != 2) throw "multi-shape composition count mismatch";
checked(localSolver.emitBurst(combined, 2));
local particles = checked(localSolver.snapshot()).particles;
if (particles.len() != 2 || particles[0].position[0] > -0.249 || particles[1].position[0] < 0.249 ||
    particles[0].color[0] < 0.99 || particles[1].color[2] < 0.99)
    throw "multi-shape order, transform or color mismatch";
local removed = checked(fluids.composeVolumeEmitterShapes(emitterBase, [right]));
if (removed.description.distribution.len() != 1 || removed.description.distribution[0].position[0] < 0.249)
    throw "multi-shape remove/update mismatch";
print("VOLUME_MULTI_SHAPE_EMITTER_PASS\n");
