// Script ECS + GPGPU ShaderSystem demo.
// Move entities with a compute shader instead of a CPU foreach loop.
// Requires Vulkan window + glslc (same as other gpgpu examples).

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

class CpuMoveSystem extends eve.System {
    constructor() { base.constructor(Moveable) }
    function update(dt) {
        foreach (e in entities()) {
            e.pos.x += e.vel.x * dt
            e.pos.y += e.vel.y * dt
            if (e.pos.x < 0.0 || e.pos.x > config.width - 24.0) e.vel.x = -e.vel.x
            if (e.pos.y < 0.0 || e.pos.y > config.height - 24.0) e.vel.y = -e.vel.y
        }
    }
}

kMoveKernel <- @"
#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Pos { float data[]; } pos;
layout(set = 0, binding = 1) buffer Vel { float data[]; } vel;
layout(push_constant) uniform PC { float data[32]; } pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint n = uint(pc.data[1]);
    if (i >= n) return;
    float dt = pc.data[0];
    uint b = i * 2u;
    pos.data[b+0u] += vel.data[b+0u] * dt;
    pos.data[b+1u] += vel.data[b+1u] * dt;
}
"

movers <- []
moveSys <- null
useGpu <- false

eve_init = function() {
    gfx.setBackgroundColor(0.08, 0.10, 0.14, 1.0)

    for (local i = 0; i < 8; ++i) {
        local e = Moveable.create()
        e.pos.x = 40.0 + i * 70.0
        e.pos.y = 80.0 + (i % 3) * 40.0
        e.vel.x = 40.0 + i * 8.0
        e.vel.y = 30.0 - i * 3.0
        movers.push(e)
    }

    useGpu = ("gpgpu" in getroottable()) && gpgpu != null && gpgpu.isAvailable()
    if (useGpu) {
        try {
            moveSys = eve.ShaderSystem(Moveable, gpgpu, kMoveKernel, 64)
            moveSys.bindFields(0, "pos", ["x", "y"])
            moveSys.bindFields(1, "vel", ["x", "y"])
            print("ecs+gpgpu: using ShaderSystem\n")
        } catch (e) {
            useGpu = false
            print("ecs+gpgpu: shader compile failed, CPU fallback: " + e + "\n")
        }
    }

    if (!useGpu)
        moveSys = CpuMoveSystem()
}

eve_update = function(dt) {
    if (moveSys != null) moveSys.update(dt)
    // Bounce on CPU so the GPU kernel stays a pure integrator.
    if (useGpu) {
        foreach (e in movers) {
            if (e.pos.x < 0.0 || e.pos.x > config.width - 24.0) e.vel.x = -e.vel.x
            if (e.pos.y < 0.0 || e.pos.y > config.height - 24.0) e.vel.y = -e.vel.y
        }
    }
}

eve_render = function() {
    gfx.clear()
    foreach (e in movers) {
        gfx.drawSolidRect(e.pos.x, e.pos.y, 24.0, 24.0, 0.35, 0.80, 0.95, 1.0)
    }
}
