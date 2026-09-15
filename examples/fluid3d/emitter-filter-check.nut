// Opt-in check for Fluid3DEmitter.Filter updating every live particle in one actor.
local definition = checked(fluids.volumeDefaults());
definition.settings.capacity = 4;
local solver = checked(fluids.newVolumeSimulator(definition));
local emission = checked(fluids.volumeEmissionDefaults());
emission.description.shape = 0;
emission.description.extent = [0.0, 0.0, 0.0];
emission.description.prototype.actorGroup = 7;
emission.description.prototype.collisionFilter = 983041;
checked(solver.emitBurst(emission, 2));
checked(solver.setActorCollisionFilter(7, 11141152));
local particles = checked(solver.snapshot()).particles;
if (particles.len() != 2 || particles[0].collisionFilter != 11141152 ||
    particles[1].collisionFilter != 11141152)
    throw "Emitter filter did not update every live actor particle";
if (solver.setActorCollisionFilter(7, 11141120).ok)
    throw "Filter with an empty category was accepted";
if (checked(solver.snapshot()).particles[0].collisionFilter != 11141152)
    throw "Rejected filter partially mutated the actor";
return "VOLUME_EMITTER_FILTER_PASS";
