#include <algorithm>
#include <cmath>
#include <new>
#include <numbers>
#include "physics/bake/collapse/CollapseAnimation.h"
#include "physics/bake/collapse/CollapseCaptureSource.h"
#include "physics/bake/collapse/CollapsePlayback.h"
#include "physics/bake/collapse/FractureGeometry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
namespace {
class Source final : public eve::physics::CollapseCaptureSource {
public:
    std::vector<eve::physics::BonePresentation>           poses = {{}, {}};
    std::shared_ptr<const eve::physics::FractureGeometry> geometry;
    int                                                   failedBone        = -1;
    bool                                                  allocationFailure = false;
    int boneCount() const noexcept override { return static_cast<int>(poses.size()); }
    std::shared_ptr<const eve::physics::FractureGeometry> fragmentGeometry() const noexcept override {
        return geometry;
    }
    eve::Result<eve::physics::BonePresentation> bonePresentation(int index) const override {
        if (allocationFailure) throw std::bad_alloc();
        if (index == failedBone)
            return eve::Result<eve::physics::BonePresentation>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle, "source retired", "fixture"));
        return eve::Result<eve::physics::BonePresentation>::success(poses.at(static_cast<std::size_t>(index)));
    }
};
}  // namespace
TEST_CASE("physics.bake.collapseCaptureInterfaceRejectsWholeFrames") {
    eve::physics::CollapseAnimation clip;
    Source                          source;
    source.geometry = std::make_shared<const eve::physics::FractureGeometry>();
    REQUIRE(!clip.capture(source, 0).ok());
    REQUIRE(clip.frameCount() == 0);
    auto malformed = std::make_shared<eve::physics::FractureGeometry>();
    malformed->fragments.resize(2);
    source.geometry = malformed;
    REQUIRE(!clip.capture(source, 0).ok());
    REQUIRE(clip.frameCount() == 0);
    source.geometry.reset();
    REQUIRE(clip.capture(source, 0).ok());
    source.poses[0].worldX = 4;
    source.failedBone      = 1;
    auto failed            = clip.capture(source, 1);
    REQUIRE(!failed.ok());
    REQUIRE(failed.status().diagnostics().front().code() == eve::DiagnosticCode::StaleHandle);
    REQUIRE(clip.frameCount() == 1);
    REQUIRE(clip.sample(1, 0).expect("unchanged").worldX == 0);
    source.failedBone           = -1;
    source.poses[1].halfExtentX = -1;
    REQUIRE(!clip.capture(source, 1).ok());
    REQUIRE(clip.frameCount() == 1);
    source.poses[1].halfExtentX = 0.75f;
    REQUIRE(!clip.capture(source, 1).ok());
    REQUIRE(clip.frameCount() == 1);
    source.poses[1].halfExtentX = 0.5f;
    source.allocationFailure    = true;
    REQUIRE(!clip.capture(source, 1).ok());
    REQUIRE(clip.frameCount() == 1);
    source.allocationFailure = false;
    REQUIRE(clip.capture(source, 1).ok());
    REQUIRE(clip.frameCount() == 2);
    REQUIRE(clip.sample(0.5f, 0).expect("interpolation").worldX == 2);
}
TEST_CASE("physics.bake.collapseCaptureInterfaceOutlivesSource") {
    eve::physics::CollapseAnimation clip;
    {
        Source source;
        REQUIRE(clip.capture(source, 0).ok());
        source.poses[0].worldX = 2;
        source.poses[1].worldX = 4;
        REQUIRE(clip.capture(source, 1).ok());
    }
    auto player = eve::physics::CollapsePlayback::prepare(std::move(clip)).expect("prepare");
    auto other  = player;
    REQUIRE(player.seek(0.5).ok());
    REQUIRE(other.seek(1).ok());
    REQUIRE(other.setPlacement({10, 0, 0}).ok());
    REQUIRE(player.sample(0).expect("first").worldX == 1);
    REQUIRE(other.sample(0).expect("second").worldX == 12);
}

TEST_CASE("physics.bake.collapseRotationCrossesWrapWithoutStateAnticipation") {
    using namespace eve::physics;
    CollapseAnimation clip;
    Source            source;
    const float       half = 179.f * std::numbers::pi_v<float> / 360.f;
    source.poses[0].rotY   = std::sin(half);
    source.poses[0].rotW   = std::cos(half);
    source.poses[1].rotY   = .6f;
    source.poses[1].rotW   = .8f;
    REQUIRE(clip.capture(source, 0).ok());
    source.poses[0].rotY  = -std::sin(half);
    source.poses[0].state = BoneRuntimeState::Detached;
    source.poses[1].rotY  = -.6f;
    source.poses[1].rotW  = -.8f;
    REQUIRE(clip.capture(source, 1).ok());
    auto player = CollapsePlayback::prepare(std::move(clip)).expect("frozen rotation");
    for (int i = 0; i <= 100; ++i) {
        const float seconds = float(i) / 100.f;
        REQUIRE(player.seek(seconds).ok());
        const auto   pose     = player.sample(0).expect("wrap sample");
        const double expected = (179. + 2. * seconds) * std::numbers::pi / 360.;
        const double dot      = double(pose.rotY) * std::sin(expected) + double(pose.rotW) * std::cos(expected);
        const double norm     = std::hypot(double(pose.rotY), double(pose.rotW));
        REQUIRE(std::abs(norm - 1.) < 1e-6);
        // Compare orientation directly; acos amplifies near-zero floating point error.
        REQUIRE(std::abs(std::abs(dot) / norm - 1.) < 1e-8);
        REQUIRE(pose.state == (i == 100 ? BoneRuntimeState::Detached : BoneRuntimeState::Attached));
        const auto antipodal = player.sample(1).expect("antipodal sample");
        REQUIRE(std::abs(std::abs(antipodal.rotY) - .6f) < 1e-6f);
        REQUIRE(std::abs(std::abs(antipodal.rotW) - .8f) < 1e-6f);
    }
    REQUIRE(player.setRate(-1).ok());
    REQUIRE(player.advance(.5).ok());
    REQUIRE(player.sample(0).expect("reverse midpoint").state == BoneRuntimeState::Attached);
    REQUIRE(std::abs(player.sample(0).expect("reverse rotation").rotW) < 1e-6f);
}

TEST_CASE("physics.bake.collapseFastRotationRequiresSufficientCadence") {
    using namespace eve::physics;
    // One revolution per second. Endpoints alone cannot distinguish this from rest.
    CollapseAnimation dense, sparse;
    {
        Source source;
        for (int i = 0; i <= 120; ++i) {
            const float seconds  = float(i) / 120.f;
            source.poses[0].rotY = std::sin(std::numbers::pi_v<float> * seconds);
            source.poses[0].rotW = std::cos(std::numbers::pi_v<float> * seconds);
            REQUIRE(dense.capture(source, seconds).ok());
            if (i == 0 || i == 120) REQUIRE(sparse.capture(source, seconds).ok());
        }
    }
    // Both own their samples after the producer dies; validate between recorded ticks too.
    double denseMaximum = 0, sparseMaximum = 0;
    for (int i = 0; i <= 240; ++i) {
        const float  seconds = float(i) / 240.f;
        const double half    = std::numbers::pi * seconds;
        for (int kind = 0; kind != 2; ++kind) {
            const auto   pose = (kind == 0 ? dense : sparse).sample(seconds, 0).expect("fast rotation");
            const double norm = std::hypot(double(pose.rotY), double(pose.rotW));
            REQUIRE(std::abs(norm - 1.) < 1e-6);
            const double dot     = double(pose.rotY) * std::sin(half) + double(pose.rotW) * std::cos(half);
            const double error   = 2 * std::acos(std::clamp(std::abs(dot) / norm, 0., 1.));
            auto&        maximum = kind == 0 ? denseMaximum : sparseMaximum;
            maximum              = std::max(maximum, error);
        }
    }
    REQUIRE(denseMaximum < 1e-5);
    REQUIRE(sparseMaximum > 3.14);
}
