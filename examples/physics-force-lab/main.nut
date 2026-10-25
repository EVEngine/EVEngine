persist lab = null

function forceVector(x, y, z = 0.0) {
    local v = eve.ForceVector(); v.x = x; v.y = y; v.z = z; return v;
}
function checked(result) {
    if (!result.ok) throw "physics-force-lab: physical model rejected input";
    return result.value;
}
function apply2(body, f) {
    checked(eve.applyForceSI2D(body, f.x, f.y, body.getX() / 30.0, body.getY() / 30.0));
}
function apply3(body, f) {
    checked(eve.applyForceSI3D(body, f.x, f.y, f.z, body.getX(), body.getY(), body.getZ()));
}

eve_init = function() {
    gfx.setBackgroundColor(0.04, 0.06, 0.10, 1.0);
    if (lab != null) return;
    physics.setMeter(30.0);
    local w2 = physics.newWorld(0.0, 0.0, false);
    local w3 = physics.newWorld3D(0.0, 0.0, 0.0, false);
    local orbit = w2.newBody("dynamic", 120.0, 0.0);
    orbit.newCircleFixture(9.0, 1.0, 0.2, 0.0); orbit.setLinearVelocity(0.0, 60.0);
    local boat = w2.newBody("dynamic", 600.0, 0.0); boat.newCircleFixture(15.0, 1.0, 0.2, 0.0);
    local plane = w3.newBody("dynamic", 0.0, 0.0, 0.0); plane.newSphereShape(0.5, 1.0, 0.2, 0.0);
    plane.setLinearVelocity(10.0, 0.0, 0.0); plane.setLinearDamping(0.0);
    local car = w3.newBody("dynamic", 0.0, 0.0, 20.0); car.newSphereShape(0.5, 1.0, 0.2, 0.0);
    car.setLinearDamping(0.0);
    local radial = eve.RadialField(); radial.strength = 16.0; radial.softening = 0.5;
    local region = eve.FieldRegion(); region.radius = 8.0; region.edgeWidth = 1.0; radial.region = region;
    local gravity = eve.DirectionalField(); gravity.acceleration = forceVector(0.0, 9.81);
    local buoyancy = eve.BuoyancyModel(); buoyancy.gravity = forceVector(0.0, 9.81);
    local damping = eve.DampingModel(); damping.linear = 1.0; damping.quadratic = 0.3;
    local wing = eve.WingModel(); wing.chord = forceVector(0.995004, 0.0998334);
    wing.normal = forceVector(-0.0998334, 0.995004); wing.area = 0.1;
    local tire = eve.TireModel(); tire.longitudinalStiffness = 15.0; tire.corneringStiffness = 15.0;
    local input = eve.TireInput(); input.normalLoad = car.getMass() * 9.81; input.driveTorque = 0.5;
    lab = { w2=w2, w3=w3, orbit=orbit, boat=boat, plane=plane, car=car,
        radial=radial, gravity=gravity, buoyancy=buoyancy, damping=damping,
        wing=wing, tire=tire, input=input, accumulator=0.0, time=0.0 };
}

eve_update = function(dt) {
    lab.accumulator += dt > 0.1 ? 0.1 : dt;
    local step = 1.0 / 120.0;
    while (lab.accumulator >= step) {
        lab.accumulator -= step; lab.time += step;
        local p = forceVector(lab.orbit.getX()/30.0, lab.orbit.getY()/30.0);
        // Every four seconds switch attraction / repulsion; force stays finite at center.
        lab.radial.strength = (lab.time.tointeger() / 4) % 2 == 0 ? 16.0 : -16.0;
        apply2(lab.orbit, checked(eve.sampleRadialForce(lab.radial, p, lab.orbit.getMass())));
        if (p.x*p.x + p.y*p.y > 49.0) {
            lab.orbit.setPosition(120.0, 0.0); lab.orbit.setLinearVelocity(0.0, 60.0);
        }
        apply2(lab.boat, checked(eve.sampleDirectionalForce(lab.gravity, forceVector(20.0, 0.0), lab.boat.getMass())));
        // Deliberately simple hull estimate: linear submerged fraction over a 1m height.
        local fraction = lab.boat.getY()/30.0 + 0.5;
        if (fraction < 0.0) fraction = 0.0; if (fraction > 1.0) fraction = 1.0;
        lab.buoyancy.displacedVolume = 2.0 * lab.boat.getMass()/1000.0 * fraction;
        apply2(lab.boat, checked(eve.sampleBuoyancyForce(lab.buoyancy)));
        apply2(lab.boat, checked(eve.sampleDampingForce(lab.damping,
            forceVector(lab.boat.getLinearVelocityX()/30.0, lab.boat.getLinearVelocityY()/30.0), lab.boat.getMass(), step)));
        local air = forceVector(lab.plane.getLinearVelocityX(), lab.plane.getLinearVelocityY(), lab.plane.getLinearVelocityZ());
        apply3(lab.plane, checked(eve.sampleWingForce(lab.wing, air)).force);
        lab.input.longitudinalVelocity = lab.car.getLinearVelocityX();
        local wheel = checked(eve.stepTire(lab.tire, lab.input, step));
        lab.input.angularVelocity = wheel.angularVelocity;
        // Contact is prescribed for this model demonstration; no road/suspension solver here.
        apply3(lab.car, {x=wheel.longitudinalForce, y=0.0, z=wheel.lateralForce});
        lab.w2.update(step); lab.w3.update(step);
        if (lab.plane.getX() > 10.0) {
            lab.plane.setPosition(0.0, 0.0, 0.0); lab.plane.setLinearVelocity(10.0, 0.0, 0.0);
        }
        if (lab.car.getX() > 10.0) {
            lab.car.setPosition(0.0, 0.0, 20.0); lab.car.setLinearVelocity(0.0, 0.0, 0.0);
            lab.input.angularVelocity = 0.0;
        }
    }
}

eve_render = function() {
    gfx.clear();
    // Upper left: 2D radial field. Upper right: 2D buoyancy at water line.
    gfx.drawSolidRect(235.0, 155.0, 10.0, 10.0, 0.7, 0.4, 1.0, 1.0);
    gfx.drawSolidRect(235.0+lab.orbit.getX(), 155.0+lab.orbit.getY(), 14.0, 14.0, 1.0, 0.6, 0.2, 1.0);
    gfx.drawSolidRect(500.0, 160.0, 430.0, 120.0, 0.05, 0.2, 0.4, 1.0);
    gfx.drawSolidRect(710.0, 150.0+lab.boat.getY(), 30.0, 30.0, 0.3, 0.9, 0.8, 1.0);
    // Lower left: 3D wing trajectory projected to XY. Lower right: driven wheel body.
    gfx.drawSolidRect(40.0+lab.plane.getX()*35.0, 480.0-lab.plane.getY()*35.0, 30.0, 8.0, 0.3, 0.7, 1.0, 1.0);
    gfx.drawSolidRect(520.0+lab.car.getX()*35.0, 480.0, 20.0, 20.0, 0.9, 0.8, 0.3, 1.0);
}
