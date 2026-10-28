// Opt-in check for Fluid3DParticleRenderer radiusScale/particleColor semantics.
local definition = checked(fluids.volumeDefaults());
definition.settings.gravity = [0.0, 0.0, 0.0];
definition.settings.capacity = 2;
local particle = checked(fluids.volumeEmissionDefaults()).description.prototype;
particle.position = [0.0, 1.0, 0.0];
particle.velocity = [2.0, 0.0, 0.0];
particle.radii = [0.1, 0.2, 0.3];
particle.color = [0.8, 0.5, 0.25, 0.75];
particle.actorGroup = 9;
definition.particles = [particle];
local solver = checked(fluids.newVolumeSimulator(definition));
checked(solver.step(1.0 / 120.0, 1));
local rows = checked(solver.particleImpostors(9, 2.0, 0.5, 0.25, 1.0, 0.5, 0.5, 1));
if (rows.len() != 1 || abs(rows[0].position[0] - (1.0 / 120.0)) > 0.0001 ||
    abs(rows[0].scale[0] - 0.2) > 0.0001 || abs(rows[0].scale[2] - 0.6) > 0.0001 ||
    abs(rows[0].color[0] - 0.4) > 0.0001 || abs(rows[0].color[3] - 0.375) > 0.0001)
    throw "particle impostor projection mismatch";
