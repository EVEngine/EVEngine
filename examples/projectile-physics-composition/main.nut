// Left: query and notify only. Right: explicitly consume the committed event as an impulse.
local labs = [];
local accumulator = 0.0;
local elapsed = 0.0;
local verified = false;
function checked(result) {
    if (!result.ok) throw result.status.summary;
    return result.value;
}
function makeLab(feedback) {
    local world = physics.newWorld3D(0.0, 0.0, 0.0, false);
    local body = world.newBody("dynamic", 2.0, 0.0, 0.0);
    local shape = body.newBoxShape(0.4, 1.0, 1.0, 1.0, 0.3, 0.0);
    local pool = checked(eve.Weapon().newCarrierRuntime(4));
    checked(pool.bindWorld3D(world.queryToken(), 0.1, -1, -1, -1));
    checked(pool.registerRecipe({id="composition:bolt", speed=10.0, lifetime=2.0, motion=["linear"],
        collision={enabled=true, worldSweep=true}, triggers=["onHit", "onExpire"],
        impacts=[{kind="emitTrigger", on="onHit"}, {kind="release", on="onHit"},
                 {kind="release", on="onExpire"}]}));
    checked(pool.spawn("composition:bolt", -2.0, 0.0, 0.0, 1.0, 0.0, 0.0));
    labs.append({world=world, body=body, shape=shape, pool=pool, feedback=feedback, hits=0});
}
eve_init = function() {
    gfx.setBackgroundColor(0.025, 0.035, 0.055, 1.0);
    makeLab(false);
    makeLab(true);
};
eve_update = function(dt) {
    accumulator += dt;
    local steps = 0;
    while (accumulator >= 1.0/120.0 && steps < 16) {
        local step = 1.0/120.0;
        foreach (lab in labs) {
            lab.world.update(step);
            local frame = checked(lab.pool.update(step));
            foreach (event in frame.events) {
                if (event.trigger != "onHit" || event.impact != "emitTrigger") continue;
                lab.hits++;
                if (lab.feedback) {
                    // Caller chooses magnitude and application point. Querying alone never applies this.
                    checked(lab.world.applySweepContactImpulse(event, lab.body.getMass()*2.0, 0.0, 0.0,
                        lab.body.getX(), lab.body.getY(), lab.body.getZ()));
                }
            }
        }
        accumulator -= step;
        elapsed += step;
        steps++;
    }
    if (!verified && elapsed > 0.6) {
        if (labs[0].hits != 1 || labs[1].hits != 1) throw "Composition requires one real contact per world";
        if (fabs(labs[0].body.getLinearVelocityX()) > 0.0001) throw "Query changed body velocity";
        if (labs[1].body.getLinearVelocityX() < 1.9) throw "Explicit impulse did not move body";
        print("PROJECTILE_PHYSICS_COMPOSITION_READY notificationOnly=1 explicitImpulse=1\n");
        verified = true;
    }
};
eve_render = function() {
    gfx.clear();
    foreach (i, lab in labs) {
        local left = 20.0 + i*450.0;
        gfx.drawSolidRect(left, 30.0, 410.0, 300.0, 0.06, 0.085, 0.12, 1.0);
        local red = lab.feedback ? 0.25 : 1.0;
        gfx.drawSolidRect(left, 30.0, 410.0, 4.0, red, 0.75, 0.65, 1.0);
        gfx.drawSolidRect(left+205.0+lab.body.getX()*32.0-6.4, 164.0, 12.8, 32.0, red, 0.75, 0.65, 1.0);
        foreach (state in checked(lab.pool.states()))
            gfx.drawSolidRect(left+205.0+state.x*32.0-3.2, 176.8, 6.4, 6.4, 0.95, 0.97, 1.0, 1.0);
    }
};
