// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "role": "tests",
  "default": "physics-backend",
  "module": "physics_backend"
}
*/
// clang-format on

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>
#include "physics/Body3D.h"
#include "physics/World3D.h"
#include "physics/backend/bake/BakeRecipe.h"
#include "physics/backend/bake/DeformTrack.h"
#include "physics/backend/bake/RigidPoseTrack.h"
#include "physics/bake_capture/RigidBakeCapture.h"
#include "physics/cloth/Cloth3D.h"
#include "physics/cloth/Cloth3DBakeCapture.h"
#include "physics/rope/Rope3D.h"
#include "physics/rope/Rope3DBakeCapture.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::physics;
namespace {
constexpr float Dt    = 1.f / 120.f;
constexpr int   Steps = 240;
// Fixture-specific regression budgets, not an accuracy promise for arbitrary effects.
double positionBudget(int stride) { return stride == 1 ? 1e-5 : stride == 2 ? .001 : stride == 4 ? .005 : .01; }
struct Error {
    double      maximum = 0, squares = 0;
    std::size_t count = 0;
    void        add(double value) {
        REQUIRE(std::isfinite(value));
        maximum = std::max(maximum, value);
        squares += value * value;
        ++count;
    }
    void report(const char* channel, int stride, const char* unit) const {
        REQUIRE(count > 0);
        std::cout << "bake_error channel=" << channel << " stride=" << stride << " unit=" << unit
                  << " samples=" << count << " max=" << maximum << " rms=" << std::sqrt(squares / count) << '\n';
    }
};
BakeRecipe provenance(const char* source, int stride) {
    BakeRecipe result;
    result.setProducer("resampling-error-v1").expect("producer");
    result.setBackend("native-cpu-reference").expect("backend");
    result.setSource("fixture", source).expect("source");
    result.setParameters("dt=1/120;steps=240;gravity=-9.81;no-random-input").expect("parameters");
    result.setTiming(Dt, stride).expect("timing");
    return result;
}
double distance(double x, double y, double z) { return std::sqrt(x * x + y * y + z * z); }
void   checkDeform(const char* name, const std::vector<DeformFrame>& reference) {
    REQUIRE(reference.size() == Steps + 1);
    double motion = 0;
    for (std::size_t vertex = 0; vertex < reference.front().positions.size(); ++vertex) {
        const auto a = reference.front().positions[vertex], b = reference.back().positions[vertex];
        motion = std::max(motion, distance(double(a.x) - b.x, double(a.y) - b.y, double(a.z) - b.z));
    }
    REQUIRE(motion > .1);
    for (int stride : {1, 2, 4, 8}) {
        std::vector<DeformFrame> sparse;
        for (int i = 0; i <= Steps; i += stride) sparse.push_back(reference[i]);
        auto clip = DeformTrack::prepare(std::move(sparse), provenance(name, stride)).expect("prepare sparse clip");
        std::vector<DeformPoint> output(clip.topology().vertexCount());
        Error                    error, retained;
        for (int i = 0; i <= Steps; ++i) {
            clip.sample(reference[i].seconds, output).expect("resample reference time");
            for (std::size_t vertex = 0; vertex < output.size(); ++vertex) {
                const auto a = output[vertex], b = reference[i].positions[vertex];
                const auto delta = distance(double(a.x) - b.x, double(a.y) - b.y, double(a.z) - b.z);
                error.add(delta);
                if (i % stride == 0) retained.add(delta);
            }
        }
        REQUIRE(retained.maximum < 1e-5);
        REQUIRE(error.maximum < positionBudget(stride));
        if (stride == 1) REQUIRE(error.maximum < 1e-5);
        error.report(name, stride, "metres");
    }
}
}  // namespace
TEST_CASE("physics.bake.deformResamplingErrorAgainstLiveReference") {
    std::vector<DeformFrame> clothFrames, ropeFrames;
    {
        Cloth3D cloth(8, 8, .2f, 0.f, 3.f, 0.f);
        cloth.setGravity(0.f, -9.81f, 0.f);
        Rope3D rope(16, 0.f, 3.f, 0.f, 3.f, 3.f, 0.f);
        rope.setGravity(0.f, -9.81f, 0.f);
        rope.pin(0).expect("pin rope");
        for (int i = 0; i <= Steps; ++i) {
            clothFrames.push_back(captureDeformFrame(cloth, i * Dt).expect("cloth reference"));
            ropeFrames.push_back(captureDeformFrame(rope, i * Dt).expect("rope reference"));
            if (i != Steps) {
                cloth.update(Dt);
                rope.update(Dt);
            }
        }
    }
    checkDeform("cloth8x8-pinned-row", clothFrames);
    checkDeform("rope16-pinned-end", ropeFrames);
}
TEST_CASE("physics.bake.rigidResamplingErrorAgainstLiveReference") {
    std::vector<RigidTrackFrame> reference;
    eve::PersistentId::Bytes     bytes{};
    bytes.back() = 1;
    const eve::PersistentId id(bytes);
    {
        World3D world(0.f, -9.81f, 0.f, false);
        auto*   body = world.newBody("dynamic", 0.f, 3.f, 0.f);
        REQUIRE(body != nullptr);
        REQUIRE(body->newBoxShape(.5f, .5f, .5f) != nullptr);
        body->setLinearVelocity(2.f, 0.f, 0.f);
        body->setAngularVelocity(0.f, 3.f, 0.f);
        body->setLinearDamping(0.f);
        body->setAngularDamping(0.f);
        const std::array bindings{RigidBakeBinding{id, world.runtimeHandle(), body->runtimeHandle()}};
        for (int i = 0; i <= Steps; ++i) {
            reference.push_back(captureRigidFrame(world, bindings, i * Dt).expect("rigid reference"));
            if (i != Steps) world.update(Dt);
        }
    }
    REQUIRE(reference.size() == Steps + 1);
    REQUIRE(reference.back().samples[0].pose.y < 0);
    for (int stride : {1, 2, 4, 8}) {
        std::vector<RigidTrackFrame> sparse;
        for (int i = 0; i <= Steps; i += stride) sparse.push_back(reference[i]);
        auto clip = RigidPoseTrack::prepare(RigidTrackSpace::Metres3D, std::move(sparse),
                                            provenance("falling-spinning-box", stride))
                        .expect("sparse rigid");
        Error position, angle;
        for (int i = 0; i <= Steps; ++i) {
            const auto a     = clip.sample(reference[i].seconds, id).expect("rigid resample");
            const auto b     = reference[i].samples[0].pose;
            const auto delta = distance(double(a.x) - b.x, double(a.y) - b.y, double(a.z) - b.z);
            position.add(delta);
            const double dot = double(a.qx) * b.qx + double(a.qy) * b.qy + double(a.qz) * b.qz + double(a.qw) * b.qw;
            const double aa  = double(a.qx) * a.qx + double(a.qy) * a.qy + double(a.qz) * a.qz + double(a.qw) * a.qw;
            const double bb  = double(b.qx) * b.qx + double(b.qy) * b.qy + double(b.qz) * b.qz + double(b.qw) * b.qw;
            angle.add(2 * std::acos(std::clamp(std::abs(dot) / std::sqrt(aa * bb), 0., 1.)));
            if (i % stride == 0) REQUIRE(delta < 1e-5);
        }
        if (stride == 1) {
            REQUIRE(position.maximum < 1e-5);
            REQUIRE(angle.maximum < 1e-6);
        }
        position.report("falling-spinning-box", stride, "metres");
        REQUIRE(position.maximum < positionBudget(stride));
        REQUIRE(angle.maximum < 1e-4);
        angle.report("falling-spinning-box", stride, "radians");
    }
}
