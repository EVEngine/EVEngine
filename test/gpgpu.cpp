#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "gpgpu/Gpgpu.h"
#include "gpgpu/ComputeShader.h"
#include "gpgpu/EcsGpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/ShaderSystem.h"
#include "graphics/Graphics.h"
#include "window/Window.h"
#include "common/ECS.h"

#include <cmath>
#include <string>
#include <vector>

using namespace eve::gpgpu;

namespace {

bool tryInitGpuWindow() {
    auto *win = eve::window::Window::create();
    auto *gfx = eve::graphics::Graphics::create();
    if (!win || !gfx) return false;
    win->setGraphics(gfx);
    eve::window::WindowSettings s;
    s.width = 320;
    s.height = 240;
    s.centered = true;
    return win->setWindowSettings(s);
}

const char *kScaleKernel = R"(#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Data {
    float values[];
} data;
layout(push_constant) uniform PC {
    float data[32];
} pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    data.values[i] *= pc.data[0];
}
)";

const char *kMoveKernel = R"(#version 450
layout(local_size_x = 64) in;
layout(set = 0, binding = 0) buffer Pos {
    float data[];
} pos;
layout(set = 0, binding = 1) buffer Vel {
    float data[];
} vel;
layout(push_constant) uniform PC {
    float data[32];
} pc;
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint n = uint(pc.data[1]);
    if (i >= n) return;
    float dt = pc.data[0];
    uint b = i * 2u;
    pos.data[b + 0u] += vel.data[b + 0u] * dt;
    pos.data[b + 1u] += vel.data[b + 1u] * dt;
}
)";

class GpuNode : public ecs::Entity {
public:
    ENTITY(GpuNode, ecs::Entity)
    void release() override { ecs::DestroyEntity(this); }

    struct Position {
        float x = 0;
        float y = 0;
    };
    COMPONENT(Position, position)

    struct Velocity {
        float dx = 0;
        float dy = 0;
    };
    COMPONENT(Velocity, velocity)
};

}  // namespace

TEST_CASE("gpgpu.module.create") {
    auto *mod = Gpgpu::create();
    REQUIRE(mod != nullptr);
    CHECK_EQ(mod->getName(), std::string("Gpgpu"));
}

TEST_CASE("gpgpu.dispatch.scaleFloats") {
    if (!tryInitGpuWindow()) return;
    auto *mod = Gpgpu::create();
    REQUIRE(mod->isAvailable());

    const int count = 128;
    GpuBuffer *buf = mod->newBuffer(count * int(sizeof(float)), "storage");
    REQUIRE(buf != nullptr);
    CHECK_EQ(buf->getSize(), count * int(sizeof(float)));

    for (int i = 0; i < count; ++i)
        buf->writeFloat32(i, float(i + 1));

    ComputeShader *shader = nullptr;
    try {
        shader = mod->newShader(kScaleKernel);
    } catch (...) {
        // glslc may be missing in some CI images — skip GPU path.
        return;
    }
    REQUIRE(shader != nullptr);
    shader->bindBuffer(0, buf);
    shader->setFloat(0, 2.f);

    const int groups = (count + 63) / 64;
    mod->dispatch(shader, groups, 1, 1);

    CHECK(std::fabs(buf->readFloat32(0) - 2.f) < 1e-4f);
    CHECK(std::fabs(buf->readFloat32(1) - 4.f) < 1e-4f);
    CHECK(std::fabs(buf->readFloat32(count - 1) - float(count) * 2.f) < 1e-3f);

    delete shader;
    delete buf;
}

TEST_CASE("gpgpu.shaderSystem.ecsMove") {
    if (!tryInitGpuWindow()) return;
    auto *mod = Gpgpu::create();
    REQUIRE(mod->isAvailable());

    ecs::Table world;
    ecs::ScopedTable guard(world);

    auto *a = GpuNode::create();
    a->position()->x = 1.f;
    a->position()->y = 2.f;
    a->velocity()->dx = 10.f;
    a->velocity()->dy = 20.f;

    auto *b = GpuNode::create();
    b->position()->x = 0.f;
    b->position()->y = 0.f;
    b->velocity()->dx = -5.f;
    b->velocity()->dy = 1.f;

    ShaderSystem sys;
    sys.setGpgpu(mod);
    try {
        sys.setShaderSource(kMoveKernel);
    } catch (...) {
        return;
    }

    const int nPos = packViewComponent<GpuNode, GpuNode::Position>(sys, 0);
    const int nVel = packViewComponent<GpuNode, GpuNode::Velocity>(sys, 1);
    REQUIRE_EQ(nPos, nVel);
    REQUIRE_GE(nPos, 2);

    sys.dispatch(nPos, 0.5f);

    unpackViewComponent<GpuNode, GpuNode::Position>(sys, 0, nPos);

    CHECK(std::fabs(a->position()->x - 6.f) < 1e-3f);
    CHECK(std::fabs(a->position()->y - 12.f) < 1e-3f);
    CHECK(std::fabs(b->position()->x - (-2.5f)) < 1e-3f);
    CHECK(std::fabs(b->position()->y - 0.5f) < 1e-3f);
}

TEST_CASE("gpgpu.buffer.bulkFloats") {
    if (!tryInitGpuWindow()) return;
    auto *mod = Gpgpu::create();
    REQUIRE(mod->isAvailable());

    std::vector<float> src = {1.f, 2.f, 3.f, 4.f};
    GpuBuffer *buf = mod->newBuffer(int(src.size() * sizeof(float)), "storage");
    buf->writeFloat32s(src.data(), int(src.size()), 0);
    std::vector<float> dst(src.size(), 0.f);
    buf->readFloat32s(dst.data(), int(dst.size()), 0);
    CHECK_EQ(dst[0], 1.f);
    CHECK_EQ(dst[3], 4.f);
    delete buf;
}
