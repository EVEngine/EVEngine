// Opt-in check for Fluid3DEmitter.KillAll in a shared volume solver.
local definition = checked(fluids.volumeDefaults());
definition.settings.capacity = 8;
local solver = checked(fluids.newVolumeSimulator(definition));
local first = checked(fluids.volumeEmissionDefaults());
first.description.shape = 0;
first.description.extent = [0.0, 0.0, 0.0];
first.description.prototype.actorGroup = 7;
checked(solver.emitBurst(first, 2));
local second = checked(fluids.volumeEmissionDefaults());
second.description.shape = 0;
second.description.origin = [0.5, 1.0, 0.0];
second.description.extent = [0.0, 0.0, 0.0];
second.description.prototype.actorGroup = 8;
checked(solver.emitBurst(second, 3));
if (checked(solver.killActorParticles(7)) != 2)
    throw "Emitter KillAll returned the wrong count";
local particles = checked(solver.snapshot()).particles;
if (particles.len() != 3) throw "Emitter KillAll removed another actor";
foreach (particle in particles)
    if (particle.actorGroup != 8) throw "Killed actor particle survived";
if (solver.killActorParticles(7).ok)
    throw "Missing actor KillAll was accepted";
return "VOLUME_EMITTER_KILL_ALL_PASS";
