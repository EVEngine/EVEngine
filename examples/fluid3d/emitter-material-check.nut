// Opt-in check for Fluid3DEmitter.UpdateParticleMaterial on existing actor particles.
local definition = checked(fluids.volumeDefaults());
definition.settings.capacity = 4;
definition.settings.spacing = 0.2;
local solver = checked(fluids.newVolumeSimulator(definition));
local source = checked(fluids.volumeEmissionDefaults());
source.description.shape = 0;
source.description.extent = [0.0, 0.0, 0.0];
source.description.prototype.actorGroup = 7;
source.description.prototype.color = [1.0, 0.0, 0.0, 1.0];
source.description.prototype.life = 3.0;
checked(solver.emitBurst(source, 2));

local material = checked(fluids.volumeEmissionDefaults());
material.description.prototype.material.viscosity = 9.0;
material.description.prototype.material.cohesion = 0.7;
material.description.prototype.data = [1.0, 2.0, 3.0, 4.0];
material.description.prototype.radii = [0.0, 0.0, 0.0];
material.description.prototype.collisionFilter = 11141152;
material.description.prototype.selfCollide = false;
checked(solver.updateActorMaterial(7, material));
foreach (particle in checked(solver.snapshot()).particles) {
    if (fabs(particle.material.viscosity - 9.0) > 0.00001 ||
        fabs(particle.radii[0] - 0.1) > 0.00001 || particle.collisionFilter != 11141152 ||
        particle.selfCollide || particle.color[0] != 1.0 || particle.life != 3.0)
        throw "Emitter material refresh mismatch";
}
material.description.prototype.material.viscosity = -1.0;
if (solver.updateActorMaterial(7, material).ok)
    throw "Invalid material refresh was accepted";
if (fabs(checked(solver.snapshot()).particles[0].material.viscosity - 9.0) > 0.00001)
    throw "Rejected material refresh partially mutated actor";
return "VOLUME_EMITTER_MATERIAL_PASS";
