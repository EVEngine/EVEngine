// Opt-in check for Fluid3DDistanceFieldRenderer-style SDF cutaway data.
local definition = checked(fluids.volumeDefaults());
definition.settings.gravity = [0.0, 0.0, 0.0];
local localSolver = checked(fluids.newVolumeSimulator(definition));
local collider = checked(fluids.volumeSdfColliderDefaults());
collider.label = 93;
collider.position = [1.0, 2.0, 3.0];
collider.scale = 2.0;
checked(localSolver.setSdfColliders([collider]));
local cutaway = checked(localSolver.debugSdfSlice(93, 2, 0.5, 0.5, 65536));
if (cutaway.width < 2 || cutaway.height < 2 ||
    cutaway.values.len() != cutaway.width * cutaway.height)
    throw "SDF slice dimensions mismatch";
local hasInside = false;
local hasOutside = false;
foreach (value in cutaway.values) {
    if (value < 0.5) hasInside = true;
    if (value > 0.5) hasOutside = true;
}
if (!hasInside || !hasOutside) throw "SDF slice did not preserve signed regions";
local rejected = localSolver.debugSdfSlice(93, 2, 0.5, 0.5, 1);
if (rejected.ok) throw "SDF slice budget was not enforced";
return format("VOLUME_SDF_SLICE_PASS %dx%d", cutaway.width, cutaway.height);
