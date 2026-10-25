#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <vector>
#include "gpgpu/ComputeShader.h"
#include "gpgpu/Gpgpu.h"
#include "gpgpu/GpuBuffer.h"
#include "gpgpu/Sequence.h"
#include "graphics/Graphics.h"
#include "physics/cloth/LayeredCloth3D.h"
#include "physics/cloth/LayeredClothGPU.h"
#include "physics/cloth/LayeredClothGpuKernel.inc"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace {
struct GpuClothFixture {
    eve::window::Window*     win = nullptr;
    eve::graphics::Graphics* gfx = nullptr;
    eve::gpgpu::Gpgpu&       initialize(zeroerr::TestContext* _ZEROERR_TEST_CONTEXT) {
        REQUIRE(std::getenv("EVENGINE_TEST_NO_RENDER") == nullptr);
        win = eve::window::Window::create();
        gfx = eve::graphics::Graphics::create();
        REQUIRE(win != nullptr);
        REQUIRE(gfx != nullptr);
        eve::window::WindowSettings settings;
        settings.width    = 320;
        settings.height   = 240;
        settings.centered = true;
        REQUIRE(win->setWindowSettings(settings));
        auto* compute = eve::gpgpu::Gpgpu::create();
        REQUIRE(compute != nullptr);
        REQUIRE(compute->isAvailable());
        return *compute;
    }
    ~GpuClothFixture() {
        if (win) win->close();
    }
};
using eve::physics::LayeredClothGPU;
void freeAll(LayeredClothGPU& cloth, float mass = 1.f) {
    for (int i = 0; i < cloth.getParticleCount(); ++i) cloth.setParticleInverseMass(i, mass);
}
// Executes the production GLSL, with a narrow-phase entry point for adversarial geometry.
// No CPU contact implementation or exported test-only physics API is involved.
struct ContactProbe {
    std::array<std::unique_ptr<eve::gpgpu::GpuBuffer>, 8> buffers;
    std::unique_ptr<eve::gpgpu::ComputeShader>            shader;
    std::unique_ptr<eve::gpgpu::Sequence>                 sequence;
    std::array<float, 32>                                 params{};
    ContactProbe(eve::gpgpu::Gpgpu& compute, int layers = 2, int cols = 2, int rows = 2) {
        std::string source = eve::physics::detail::layeredClothKernel;
        source.replace(source.find("void main()"), 11, "void simulationMain()");
        source += R"glsl(
void main() {
    if(pc.d[28]<0.5) {simulationMain();return;}
    if(gl_GlobalInvocationID.x!=0)return;
    vec3 j=vec3(0),w=vec3(0);bool hit;
    if(pc.d[28]<1.5)hit=contact(0,uint(pc.d[29]),w,j);
    else {vec2 ew;hit=edgeContact(uvec2(0,1),uvec2(uint(pc.d[30]),uint(pc.d[30])+1),ew,j);w=vec3(ew,0);}
    b[0]=hit?vec4(j,1):vec4(0);b[1]=hit?vec4(w,0):vec4(0);
})glsl";
        shader.reset(compute.newShader(source));
        sequence.reset(compute.newSequence());
        const int count = layers * cols * rows, triangles = layers * (cols - 1) * (rows - 1) * 2;
        int       buckets = 64;
        while (buckets < count * 2) buckets *= 2;
        const int                capacity = std::max(1024, 32 * (count + triangles));
        const std::array<int, 8> sizes    = {count * 16,  count * 16,    count * 16, count * 192,
                                             buckets * 4, capacity * 32, 32,         count * 176};
        for (size_t i = 0; i < 8; i++) {
            buffers[i].reset(compute.newBuffer(sizes[i], "storage"));
            shader->bindBuffer(int(i), buffers[i].get());
        }
        params[1]  = float(count);
        params[2]  = float(cols);
        params[3]  = float(rows);
        params[4]  = float(layers);
        params[6]  = 0.02f;
        params[16] = 1;
        params[19] = float(buckets);
        params[20] = float(capacity);
        params[30] = 4;
        params[27] = -100;
        params[21] = float(triangles);
        params[23] = 0.2f;
        params[24] = 1;
        params[26] = 1;
    }
    template <class State>
    State run(const State& current, const State& previous, int probe, int iterations = 1) {
        const size_t bytes = current.size() * sizeof(float);
        buffers[0]->uploadBytes(current.data(), bytes);
        buffers[2]->uploadBytes(previous.data(), bytes);
        params[28] = float(probe);
        for (int i = 0; i < 32; i++) shader->setFloat(i, params[size_t(i)]);
        sequence->begin();
        if (probe)
            sequence->recordDispatch(shader.get(), 1);
        else
            for (int iteration = 0; iteration < iterations; iteration++) {
                for (int mode : {2, 3, 6, 4}) {
                    shader->setFloat(7, float(mode));
                    sequence->recordDispatch(shader.get(), (int(std::max(params[19], params[21])) + 63) / 64);
                }
                std::swap(buffers[0], buffers[1]);
                shader->bindBuffer(0, buffers[0].get());
                shader->bindBuffer(1, buffers[1].get());
            }
        sequence->submit();
        State output = current;
        buffers[probe ? 1 : 0]->downloadBytes(output.data(), bytes);
        return output;
    }
};
std::array<float, 32> pointFacePose(float y) {
    // Only particle 0 is mobile; the target is the interior of triangle (4,6,5).
    return {-0.25f, y, -0.25f, 1, -0.25f, y, -0.25f, 0, -0.25f, y, -0.25f, 0, -0.25f, y, -0.25f, 0,
            -1,     0, -1,     0, 1,      0, -1,     0, -1,     0, 1,      0, 1,      0, 1,      0};
}
std::array<float, 32> edgePose(float y) {
    // Two perpendicular edge interiors cross; all four endpoints miss the other edge.
    return {-1, y, 0, 1, 1, y, 0, 1, -1, y, 0, 0, 1, y, 0, 0, 0, 0, -1, 0, 0, 0, 1, 0, 0, 0, -1, 0, 0, 0, 1, 0};
}
}  // namespace
TEST_CASE("PhysicsLayeredClothGPU.freeFallMatchesReferenceAndKeepsResidentState") {
    GpuClothFixture        fixture;
    auto&                  compute = fixture.initialize(_ZEROERR_TEST_CONTEXT);
    [[maybe_unused]] auto* gfx     = fixture.gfx;
    LayeredClothGPU cloth(compute, *gfx, 2, 3, 3, 0.25f, 0.1f, 0, 2, 0);
    cloth.setSolver(1, 4);
    cloth.setInterCollision(false);
    cloth.setGravity(0, -3, 0);
    freeAll(cloth);
    eve::physics::LayeredCloth3D reference(2, 3, 3, 0.25f, 0.1f, 0, 2, 0);
    reference.setSolver(1, 1);
    reference.setInterCollision(false);
    for (int layer = 0; layer < 2; ++layer) {
        auto& sheet = reference.layer(layer);
        sheet.setGravity(0, -3, 0);
        sheet.setDamping(0.005f);
        sheet.setStiffness(0);
        sheet.setStretchCompliance(0);
        sheet.setBendCompliance(0);
        sheet.setSelfCollision(false);
        sheet.setFoldStiffness(0);
        for (int i = 0; i < 9; ++i) sheet.setParticleInverseMass(i, 1);
    }
    float displacement = 0, velocity = 0;
    for (int frame = 0; frame < 8; ++frame) {
        auto step = cloth.update(0.01f);
        REQUIRE(step.ok());
        reference.update(0.01f);
        velocity = velocity * 0.995f - 3.f * 0.01f * 0.01f;
        displacement += velocity;
    }
    auto data = cloth.readPositions();
    REQUIRE(data.size() == 72);
    for (int i = 0; i < 18; ++i) {
        REQUIRE(std::abs(data[size_t(i) * 4 + 1] - (2.f + (i / 9) * 0.1f + displacement)) < 1e-4f);
        REQUIRE(std::abs(data[size_t(i) * 4] - float(i % 3) * 0.25f) < 1e-5f);
        REQUIRE(std::abs(data[size_t(i) * 4 + 1] - reference.layer(i / 9).getParticleY(i % 9)) < 1e-4f);
    }
    auto invalid = cloth.update(std::numeric_limits<float>::quiet_NaN());
    REQUIRE(!invalid.ok());
    auto tiny = cloth.update(1e-30f);
    REQUIRE(!tiny.ok());
    auto after = cloth.readPositions();
    REQUIRE(after == data);
    auto stats = cloth.readDiagnostics();
    REQUIRE(stats[2] == 0);
}
TEST_CASE("PhysicsLayeredClothGPU.pinnedPoseAndReset") {
    GpuClothFixture        fixture;
    auto&                  compute = fixture.initialize(_ZEROERR_TEST_CONTEXT);
    [[maybe_unused]] auto* gfx     = fixture.gfx;
    LayeredClothGPU cloth(compute, *gfx, 3, 4, 4, 0.25f, 0.09f, 0, 2, 0);
    cloth.setSolver(2, 8);
    cloth.setWind(0.2f, 0.1f, 0.3f);
    for (int i = 0; i < 12; ++i) {
        auto r = cloth.update(1.f / 60.f);
        REQUIRE(r.ok());
    }
    auto data = cloth.readPositions();
    for (int l = 0; l < 3; ++l)
        for (int c = 0; c < 4; ++c) {
            size_t i = size_t(l * 16 + c) * 4;
            REQUIRE(std::abs(data[i + 1] - (2.f + float(l) * 0.09f)) < 1e-6f);
        }
    for (float v : data) REQUIRE(std::isfinite(v));
    REQUIRE(data[4 * 12 + 1] < 2.f - 0.01f);
    cloth.reset();
    data = cloth.readPositions();
    REQUIRE(data[4 * 12 + 1] == 2.f);
    REQUIRE(cloth.readDiagnostics()[2] == 0);
}
TEST_CASE("PhysicsLayeredClothGPU.contactsReactByMassAndCanBeDisabled") {
    GpuClothFixture        fixture;
    auto&                  compute = fixture.initialize(_ZEROERR_TEST_CONTEXT);
    [[maybe_unused]] auto* gfx     = fixture.gfx;
    LayeredClothGPU cloth(compute, *gfx, 2, 2, 2, 0.5f, 0.02f, 0, 2, 0);
    cloth.setGravity(0, 0, 0);
    cloth.setSolver(1, 16);
    cloth.setContact(0.08f, 0);
    for (int i = 0; i < 8; ++i) cloth.setParticleInverseMass(i, i < 4 ? 1.f : 0.25f);
    auto step = cloth.update(0.01f);
    REQUIRE(step.ok());
    auto positions = cloth.readPositions();
    auto stats     = cloth.readDiagnostics();
    REQUIRE(stats[2] > 0);
    float weighted = 0, lower = 0, upper = 0;
    for (int i = 0; i < 8; ++i) {
        float y = positions[size_t(i) * 4 + 1];
        weighted += y * (i < 4 ? 1.f : 4.f);
        (i < 4 ? lower : upper) += y;
    }
    REQUIRE(std::abs(weighted / 20.f - 2.016f) < 2e-4f);
    REQUIRE((upper - lower) / 4.f > 0.07f);
    cloth.reset();
    cloth.setInterCollision(false);
    freeAll(cloth);
    step = cloth.update(0.01f);
    REQUIRE(step.ok());
    positions = cloth.readPositions();
    REQUIRE(std::abs(positions[4 * 4 + 1] - positions[1] - 0.02f) < 1e-5f);
    REQUIRE(cloth.readDiagnostics()[2] == 0);
}
TEST_CASE("PhysicsLayeredClothGPU.hashOverflowUsesExplicitGpuScan") {
    GpuClothFixture        fixture;
    auto&                  compute = fixture.initialize(_ZEROERR_TEST_CONTEXT);
    [[maybe_unused]] auto* gfx     = fixture.gfx;
    LayeredClothGPU cloth(compute, *gfx, 2, 2, 2, 0.001f, 0.001f, 0, 2, 0);
    cloth.setSolver(1, 1);
    cloth.setGravity(0, 0, 0);
    cloth.setContact(1.f, 0);
    auto result = cloth.update(0.01f);
    REQUIRE(result.ok());
    auto stats = cloth.readDiagnostics();
    REQUIRE(stats[1] == 1);
    REQUIRE(stats[3] > 0);
    auto positions = cloth.readPositions();
    for (float value : positions) REQUIRE(std::isfinite(value));
}
TEST_CASE("PhysicsLayeredClothGPU.rendersComputeGeneratedVertices") {
    GpuClothFixture        fixture;
    auto&                  compute = fixture.initialize(_ZEROERR_TEST_CONTEXT);
    [[maybe_unused]] auto* gfx     = fixture.gfx;
    LayeredClothGPU cloth(compute, *gfx, 3, 6, 6, 0.25f, 0.12f, -0.6f, 1, -0.6f);
    cloth.setWind(1, 0, 0.4f);
    for (int frame = 0; frame < 12; frame++) {
        auto step = cloth.update(1.f / 60.f);
        REQUIRE(step.ok());
    }
    const auto deformedPositions = cloth.readPositions();
    REQUIRE(deformedPositions[4 * 30 + 1] < 0.98f);
    gfx->setScreenReadbackEnabled(true);
    auto view = glm::lookAtRH(glm::vec3(2, 3, 4), glm::vec3(0, 1, 0), glm::vec3(0, 1, 0));
    auto proj = glm::perspectiveRH_ZO(glm::radians(45.f), 320.f / 240.f, 0.1f, 100.f);
    proj[1][1] *= -1;
    for (int i = 0; i < 3; ++i) {
        gfx->begin3DFrame();
        gfx->setMesh3DViewProj(proj * view);
        gfx->setMesh3DView(view);
        gfx->setMesh3DClip(0.1f, 100);
        gfx->setMesh3DCameraPos(glm::vec3(2, 3, 4));
        auto draw = cloth.draw();
        REQUIRE(draw.ok());
        gfx->present();
    }
    const auto path = std::filesystem::temp_directory_path() / "layered-cloth-gpu-test.png";
    REQUIRE(gfx->saveFramePng(path.string()));
    REQUIRE(std::filesystem::file_size(path) > 300);
    std::ifstream           deformedFile(path, std::ios::binary);
    const std::vector<char> deformed((std::istreambuf_iterator<char>(deformedFile)), {});
    cloth.reset();
    for (int i = 0; i < 3; ++i) {
        gfx->begin3DFrame();
        gfx->setMesh3DViewProj(proj * view);
        gfx->setMesh3DView(view);
        gfx->setMesh3DClip(0.1f, 100);
        gfx->setMesh3DCameraPos(glm::vec3(2, 3, 4));
        auto draw = cloth.draw();
        REQUIRE(draw.ok());
        gfx->present();
    }
    const auto resetPath = std::filesystem::temp_directory_path() / "layered-cloth-gpu-reset-test.png";
    REQUIRE(gfx->saveFramePng(resetPath.string()));
    std::ifstream           resetFile(resetPath, std::ios::binary);
    const std::vector<char> rest((std::istreambuf_iterator<char>(resetFile)), {});
    REQUIRE(deformed != rest);
}

TEST_CASE("PhysicsLayeredClothGPU.ccdPointFacePreservesStartingSide") {
    GpuClothFixture        fixture;
    auto&                  compute = fixture.initialize(_ZEROERR_TEST_CONTEXT);
    [[maybe_unused]] auto* gfx     = fixture.gfx;
    ContactProbe probe(compute);
    probe.params[29] = 2;
    auto previous = pointFacePose(0.8f), current = pointFacePose(-0.8f);
    auto result = probe.run(current, previous, 1);
    REQUIRE(result[3] == 1);
    REQUIRE(std::abs(result[1] - 0.82f) < 1e-4f);
    REQUIRE(std::abs(result[0]) + std::abs(result[2]) < 1e-5f);
    // Starting within the contact shell must not tunnel on the next substep.
    previous = pointFacePose(0.01f);
    result   = probe.run(current, previous, 1);
    REQUIRE(result[3] == 1);
    REQUIRE(result[1] > 0.8f);
    previous = pointFacePose(-0.8f);
    current  = pointFacePose(0.8f);
    result   = probe.run(current, previous, 1);
    REQUIRE(result[3] == 1);
    REQUIRE(result[1] < -0.8f);
    // A fast trajectory outside the triangle is a miss, not a speculative plane hit.
    previous[0] = current[0] = 4;
    result                   = probe.run(current, previous, 1);
    REQUIRE(result[3] == 0);
    previous = pointFacePose(0.8f);
    current  = previous;
    for (int i = 4; i < 8; i++) current[size_t(i * 4 + 1)] = 1.6f;
    result = probe.run(current, previous, 1);
    REQUIRE(result[3] == 1);  // Moving face sweeps through a stationary point.
    REQUIRE(std::abs(result[1] - 0.82f) < 1e-4f);
}
TEST_CASE("PhysicsLayeredClothGPU.ccdEdgeInteriorsAndDegeneracy") {
    GpuClothFixture        fixture;
    auto&                  compute = fixture.initialize(_ZEROERR_TEST_CONTEXT);
    [[maybe_unused]] auto* gfx     = fixture.gfx;
    ContactProbe probe(compute);
    auto         previous = edgePose(0.6f), current = edgePose(-0.6f);
    auto         result = probe.run(current, previous, 2);
    REQUIRE(result[3] == 1);
    REQUIRE(std::abs(result[4] - 0.5f) < 1e-4f);
    REQUIRE(std::abs(result[5] - 0.5f) < 1e-4f);
    REQUIRE(result[1] > 1.2f);  // denominator is 0.5 for the mobile edge.
    previous = edgePose(0.01f);
    current  = previous;
    result   = probe.run(current, previous, 2);
    REQUIRE(result[3] == 1);
    REQUIRE(result[1] > 0);
    // Collapse one segment: still a finite point/segment constraint.
    for (int k = 0; k < 3; k++) current[size_t(k + 4)] = current[size_t(k)];
    previous = current;
    result   = probe.run(current, previous, 2);
    for (float value : result) REQUIRE(std::isfinite(value));
}
TEST_CASE("PhysicsLayeredClothGPU.sweptHashAndScanKeepCrossingPointAboveFace") {
    GpuClothFixture        fixture;
    auto&                  compute = fixture.initialize(_ZEROERR_TEST_CONTEXT);
    [[maybe_unused]] auto* gfx     = fixture.gfx;
    ContactProbe probe(compute);
    auto         previous = pointFacePose(0.8f), current = pointFacePose(-0.8f);
    auto         hashed = probe.run(current, previous, 0, 48);
    REQUIRE(hashed[1] > 0.019f);
    REQUIRE(hashed[1] < 0.021f);
    std::array<unsigned, 4> stats{};
    probe.buffers[6]->downloadBytes(stats.data(), 16);
    REQUIRE(stats[1] == 0);
    probe.params[20] = 1;  // Exhaust reference capacity deliberately, not just the per-box cell limit.
    auto scanned     = probe.run(current, previous, 0, 48);
    REQUIRE(std::abs(scanned[1] - hashed[1]) < 1e-5f);
    probe.buffers[6]->downloadBytes(stats.data(), 16);
    REQUIRE(stats[1] == 1);
    REQUIRE(stats[3] > 0);
    for (int i = 4; i < 8; i++)
        for (int k = 0; k < 4; k++) REQUIRE(scanned[size_t(i * 4 + k)] == current[size_t(i * 4 + k)]);
}
TEST_CASE("PhysicsLayeredClothGPU.sweptEdgesReactWithBalancedMass") {
    GpuClothFixture        fixture;
    auto&                  compute = fixture.initialize(_ZEROERR_TEST_CONTEXT);
    [[maybe_unused]] auto* gfx     = fixture.gfx;
    ContactProbe probe(compute);
    auto         previous = edgePose(0.6f), current = edgePose(-0.6f);
    // Different physical masses on the two edge endpoints.
    current[7] = previous[7] = 0.25f;
    auto solved              = probe.run(current, previous, 0, 48);
    REQUIRE((solved[1] + solved[5]) * 0.5f > 0.015f);
    // Free every vertex: pinned duplicate endpoints would be external supports,
    // so the mobile vertices alone would not form a closed system.
    for (int i = 0; i < 8; i++)
        current[size_t(i * 4 + 3)] = previous[size_t(i * 4 + 3)] = i < 4 ? (i % 2 ? 0.25f : 1.f) : 0.5f;
    solved       = probe.run(current, previous, 0, 1);
    float before = 0, after = 0;
    for (int i = 0; i < 8; i++) {
        before += current[size_t(i * 4 + 1)] / current[size_t(i * 4 + 3)];
        after += solved[size_t(i * 4 + 1)] / current[size_t(i * 4 + 3)];
    }
    REQUIRE(std::abs(before - after) < 1e-4f);
}

TEST_CASE("PhysicsLayeredClothGPU.contactFrictionAndSelfFiltering") {
    GpuClothFixture        fixture;
    auto&                  compute = fixture.initialize(_ZEROERR_TEST_CONTEXT);
    [[maybe_unused]] auto* gfx     = fixture.gfx;
    ContactProbe probe(compute);
    probe.params[29] = 2;
    probe.params[17] = 0.5f;
    auto previous = pointFacePose(0.01f), current = previous;
    current[0] += 0.1f;
    auto result = probe.run(current, previous, 1);
    REQUIRE(result[3] == 1);
    REQUIRE(std::abs(result[0] + 0.005f) < 1e-5f);
    REQUIRE(std::abs(result[1] - 0.01f) < 1e-5f);
    current = pointFacePose(0.4f);
    result  = probe.run(current, previous, 1);
    REQUIRE(result[3] == 0);  // separating motion is not stuck to its starting surface.
    probe.params[3]  = 4;
    probe.params[4]  = 1;
    probe.params[16] = 0;
    probe.params[18] = 1;
    probe.params[30] = 6;
    previous         = edgePose(0.6f);
    current          = edgePose(-0.6f);
    for (int k = 0; k < 8; k++) {
        previous[size_t(24 + k)] = previous[size_t(16 + k)];
        current[size_t(24 + k)]  = current[size_t(16 + k)];
    }
    result = probe.run(current, previous, 2);
    REQUIRE(result[3] == 1);  // nonlocal edges in the same sheet.
    probe.params[18] = 0;
    result           = probe.run(current, previous, 2);
    REQUIRE(result[3] == 0);
    probe.params[18] = 1;
    probe.params[30] = 4;
    result           = probe.run(current, previous, 2);
    REQUIRE(result[3] == 0);  // local topological neighbours remain excluded.
}


TEST_CASE("PhysicsLayeredClothGPU.denseContactsBoundCorrectionAndConserveCentre") {
    GpuClothFixture        fixture;
    auto&                  compute = fixture.initialize(_ZEROERR_TEST_CONTEXT);
    [[maybe_unused]] auto* gfx     = fixture.gfx;
    ContactProbe probe(compute, 8);
    probe.params[6] = 0.1f;
    std::vector<float> pose(32 * 4);
    for (int i = 0; i < 32; i++) {
        pose[size_t(i * 4)]     = float(i % 2) * 0.5f;
        pose[size_t(i * 4 + 1)] = float(i / 4) * 0.001f;
        pose[size_t(i * 4 + 2)] = float((i % 4) / 2) * 0.5f;
        pose[size_t(i * 4 + 3)] = 1;
    }
    const auto solved  = probe.run(pose, pose, 0);
    float      maxMove = 0, before = 0, after = 0;
    for (int i = 0; i < 32; i++) {
        maxMove = std::max(maxMove, std::abs(solved[size_t(i * 4 + 1)] - pose[size_t(i * 4 + 1)]));
        before += pose[size_t(i * 4 + 1)];
        after += solved[size_t(i * 4 + 1)];
    }
    std::cout << "dense contact max correction=" << maxMove << ", centre error=" << std::abs(before - after) / 32
              << '\n';
    REQUIRE(maxMove <= 0.1f);
    REQUIRE(std::abs(before - after) / 32 < 1e-6f);
}

TEST_CASE("PhysicsLayeredClothGPU.foldedSelfContactSeparatesWithoutCentreDrift") {
    GpuClothFixture        fixture;
    auto&                  compute = fixture.initialize(_ZEROERR_TEST_CONTEXT);
    [[maybe_unused]] auto* gfx     = fixture.gfx;
    ContactProbe probe(compute, 1, 2, 8);
    probe.params[6]  = 0.08f;
    probe.params[16] = 0;
    probe.params[18] = 1;
    std::vector<float> pose(16 * 4);
    for (int row = 0; row < 8; row++)
        for (int col = 0; col < 2; col++) {
            const size_t i = size_t((row * 2 + col) * 4);
            pose[i]        = float(col) * 0.5f;
            pose[i + 1]    = row < 4 ? 0 : 0.01f;
            pose[i + 2]    = float(row < 4 ? row : 7 - row) * 0.5f;
            pose[i + 3]    = row < 4 ? 1.f : 0.25f;
        }
    const auto solved = probe.run(pose, pose, 0, 96);
    float      before = 0, after = 0, maxMove = 0;
    for (int i = 0; i < 16; i++) {
        before += pose[size_t(i * 4 + 1)] / pose[size_t(i * 4 + 3)];
        after += solved[size_t(i * 4 + 1)] / pose[size_t(i * 4 + 3)];
        maxMove = std::max(maxMove, std::abs(solved[size_t(i * 4 + 1)] - pose[size_t(i * 4 + 1)]));
    }
    float minGap = 100;
    for (int col = 0; col < 2; col++)
        minGap = std::min(minGap, solved[size_t((14 + col) * 4 + 1)] - solved[size_t(col * 4 + 1)]);
    std::cout << "folded contact: end-row separation=" << minGap << ", max correction=" << maxMove << '\n';
    REQUIRE(minGap > 0.075f);
    REQUIRE(maxMove < 0.15f);
    REQUIRE(std::abs(before - after) < 1e-5f);
    for (float value : solved) REQUIRE(std::isfinite(value));
}
