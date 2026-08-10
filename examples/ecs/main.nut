// Script ECS demo — define Component / Entity classes, drive with a System.
// Use as main.nut (or copy the class definitions into your game).
// GPU variant: see gpu_main.nut (eve.ShaderSystem + compute shader).

class Position extends eve.Component {
    x = 0.0
    y = 0.0
}

class Velocity extends eve.Component {
    x = 0.0
    y = 0.0
}

class Moveable extends eve.Entity {
    pos = Position
    vel = Velocity
}

class MoveSystem extends eve.System {
    constructor() {
        base.constructor(Moveable)
    }
    function update(dt) {
        foreach (e in entities()) {
            e.pos.x += e.vel.x * dt
            e.pos.y += e.vel.y * dt
            // bounce in the window
            if (e.pos.x < 0.0 || e.pos.x > config.width - 40.0) e.vel.x = -e.vel.x
            if (e.pos.y < 0.0 || e.pos.y > config.height - 40.0) e.vel.y = -e.vel.y
        }
    }
}

mover <- null
moveSys <- null

eve_init = function() {
    gfx.setBackgroundColor(0.10, 0.12, 0.18, 1.0)
    mover = Moveable.create()
    mover.pos.x = 120.0
    mover.pos.y = 90.0
    mover.vel.x = 80.0
    mover.vel.y = 55.0
    moveSys = MoveSystem()
}

eve_update = function(dt) {
    moveSys.update(dt)
}

eve_render = function() {
    gfx.clear()
    gfx.drawSolidRect(mover.pos.x, mover.pos.y, 40.0, 40.0, 0.35, 0.75, 0.95, 1.0)
}
