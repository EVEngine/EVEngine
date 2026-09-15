// Opt-in check for Fluid3DEmitter.useShapeColor across burst and moving-jet emission.
local definition = checked(fluids.volumeDefaults());
definition.settings.capacity = 16;
definition.settings.gravity = [0.0, 0.0, 0.0];
local emission = checked(fluids.volumeEmissionDefaults());
emission.description.shape = 5;
emission.description.direction = [0.0, 0.0, 1.0];
emission.description.speed = 6.0;
emission.description.prototype.color = [0.8, 0.6, 0.4, 1.0];
emission.description.distribution = [{
    position = [0.0, 0.0, 0.0],
    color = [0.5, 0.25, 0.1, 0.5],
    direction = [0.0, 0.0, 1.0]
}];

local colored = checked(fluids.newVolumeSimulator(definition));
if (checked(colored.emitBurst(emission, 1)) != 1) throw "Shape-colored burst failed";
local color = checked(colored.snapshot()).particles[0].color;
if (fabs(color[0] - 0.5) > 0.00001 || fabs(color[1] - 0.25) > 0.00001 ||
    fabs(color[2] - 0.1) > 0.00001 || fabs(color[3] - 0.5) > 0.00001)
    throw "Shape color was not assigned";

emission.description.useShapeColor = false;
local plain = checked(fluids.newVolumeSimulator(definition));
local jet = eve.VolumeFluidJetEmitter();
if (checked(jet.advance(plain, emission, 1.0 / 30.0, 4, 0.0)) < 1)
    throw "Uncolored moving jet failed";
foreach (particle in checked(plain.snapshot()).particles) {
    color = particle.color;
    if (fabs(color[0] - 0.8) > 0.00001 || fabs(color[1] - 0.6) > 0.00001 ||
        fabs(color[2] - 0.4) > 0.00001 || fabs(color[3] - 1.0) > 0.00001)
        throw "Disabled shape color changed the prototype color";
}
print("VOLUME_SHAPE_COLOR_ASSIGNMENT_PASS\n");
return "VOLUME_SHAPE_COLOR_PASS";
