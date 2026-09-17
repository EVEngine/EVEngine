local definition = checked(fluids.volumeDefaults());
definition.particles = [];
local solver = checked(fluids.newVolumeSimulator(definition));
local prototype = checked(fluids.volumeEmissionDefaults()).description.prototype;
local a = clone prototype;
local b = clone prototype;
local c = clone prototype;
a.position = [-1.90, 0.10, -0.90];
b.position = [-1.85, 0.15, -0.85];
c.position = [-1.50, 0.10, -0.90];
checked(solver.emit([a, b, c]));
local cells = checked(solver.debugParticleGrid(2));
if (cells.len() != 2) throw "particle grid occupied-cell count mismatch";
if (cells[0].particleCount != 2 || cells[1].particleCount != 1)
    throw "particle grid population mismatch";
if (solver.debugParticleGrid(1).ok) throw "particle grid budget accepted partial output";
::gridDebuggerPass <- true;
