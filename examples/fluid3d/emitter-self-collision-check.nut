// Opt-in check for Fluid3DEmitter.SetSelfCollisions on existing actor particles.
local definition = checked(fluids.volumeDefaults());
definition.settings.capacity = 4;
local solver = checked(fluids.newVolumeSimulator(definition));
local emission = checked(fluids.volumeEmissionDefaults());
emission.description.shape = 0;
emission.description.extent = [0.0, 0.0, 0.0];
emission.description.prototype.actorGroup = 7;
checked(solver.emitBurst(emission, 2));
checked(solver.setActorSelfCollisions(7, false));
foreach (particle in checked(solver.snapshot()).particles)
    if (particle.selfCollide) throw "Actor self-collision disable was not propagated";
checked(solver.setActorSelfCollisions(7, true));
foreach (particle in checked(solver.snapshot()).particles)
    if (!particle.selfCollide) throw "Actor self-collision enable was not propagated";
if (solver.setActorSelfCollisions(9, false).ok)
    throw "Missing actor self-collision update was accepted";
return "VOLUME_EMITTER_SELF_COLLISION_PASS";
