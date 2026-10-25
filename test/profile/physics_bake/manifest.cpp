#include <array>
#include <filesystem>
#include <iostream>
#include <random>
#include <stdexcept>
#include "physics/backend/bake/BakeRecipe.h"
#include "physics/backend/bake/BakedParticlePlayback.h"
#include "physics/backend/bake/DeformPlayback.h"
#include "physics/backend/bake/RigidPlayback.h"
#include "physics/backend/bake/collapse/CollapseAnimation.h"
#include "physics/backend/bake/collapse/CollapseCaptureSource.h"
#include "physics/backend/bake/collapse/CollapsePlayback.h"

namespace {
void require(bool condition) {
    if (!condition) throw std::runtime_error("manifest playback contract failed");
}
struct Files {
    std::filesystem::path directory;
    Files() {
        for (int attempt = 0; attempt < 100; ++attempt) {
            const auto candidate = std::filesystem::temp_directory_path() /
                                   ("eve-bake-manifest-" + std::to_string(std::random_device{}()));
            if (std::filesystem::create_directory(candidate)) {
                directory = candidate;
                return;
            }
        }
        throw std::runtime_error("cannot reserve fixture directory");
    }
    ~Files() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};
class CollapseSource final : public eve::physics::CollapseCaptureSource {
public:
    eve::physics::BonePresentation              pose;
    int                                         boneCount() const noexcept override { return 1; }
    eve::Result<eve::physics::BonePresentation> bonePresentation(int index) const override {
        require(index == 0);
        return eve::Result<eve::physics::BonePresentation>::success(pose);
    }
    std::shared_ptr<const eve::physics::FractureGeometry> fragmentGeometry() const noexcept override { return {}; }
};
void run() {
    using namespace eve::physics;
    Files      files;
    BakeRecipe recipe;
    recipe.setProducer("manifest-playback-v1").expect("producer");
    recipe.setBackend("fixture-values").expect("backend");
    recipe.setParameters("linear").expect("parameters");
    recipe.setSource("fixture", "two-frames").expect("source");
    recipe.setTiming(1.0f, 1).expect("timing");
    recipe.setRandomStream("fixture", 1).expect("seed");
    eve::PersistentId::Bytes bytes{};
    bytes.back() = 1;
    const eve::PersistentId id(bytes);
    const auto              rigidPath = (files.directory / "rigid.bake").string();
    RigidPoseTrack::prepare(RigidTrackSpace::Metres3D, {{0, {{id, {}}}}, {1, {{id, {2, 0, 0, 0, 0, 0, 1}}}}}, recipe)
        .expect("rigid fixture")
        .save(rigidPath)
        .expect("save rigid");
    RigidPlayback rigid(RigidPoseTrack::loadForRecipe(rigidPath, recipe).expect("load rigid"));
    require(std::filesystem::remove(rigidPath));
    rigid.seek(0.5).expect("rigid seek");
    require(rigid.sample(id).expect("rigid sample").x == 1);
    auto other = rigid;
    other.setPlacement({10, 0, 0}).expect("rigid placement");
    require(other.sample(id).expect("instance sample").x == 11);
    require(rigid.sample(id).expect("original sample").x == 1);

    const auto topology   = DeformTopology::prepare(DeformPrimitive::Segments, 2, {0, 1}, {}).expect("topology");
    const auto deformPath = (files.directory / "deform.bake").string();
    DeformTrack::prepare({{0, topology, {{0, 0, 0}, {1, 0, 0}}}, {1, topology, {{2, 0, 0}, {3, 0, 0}}}}, recipe)
        .expect("deform fixture")
        .save(deformPath)
        .expect("save deform");
    auto deform = DeformPlayback::prepare(DeformTrack::loadForRecipe(deformPath, recipe).expect("load deform"))
                      .expect("prepare deform");
    require(std::filesystem::remove(deformPath));
    std::array<DeformPoint, 2> points{};
    deform.seek(0.5).expect("deform seek");
    deform.sample(points).expect("deform sample");
    require(points[0].x == 1 && points[1].x == 2);

    const auto collapsePath = (files.directory / "collapse.bake").string();
    {
        CollapseAnimation clip;
        clip.beginBake(recipe).expect("collapse provenance");
        CollapseSource source;
        clip.capture(source, 0).expect("collapse first");
        source.pose.worldX = 2;
        clip.capture(source, 1).expect("collapse last");
        clip.save(collapsePath).expect("save collapse");
    }
    auto collapse =
        CollapsePlayback::prepare(CollapseAnimation::loadForRecipe(collapsePath, recipe).expect("load collapse"))
            .expect("prepare collapse");
    require(std::filesystem::remove(collapsePath));
    collapse.seek(0.5).expect("collapse seek");
    require(collapse.sample(0).expect("collapse sample").worldX == 1);

    recipe.setCoordinates(BakeCoordinates::PlanarPixels).expect("particle units");
    BakedParticlePose start;
    start.w = start.h       = 2;
    auto end                = start;
    end.x                   = 2;
    const auto particlePath = (files.directory / "particles.bake").string();
    ParticleTrack::prepare(1, {{id, 1, {{0, start}, {1, end}}}}, recipe)
        .expect("particle fixture")
        .save(particlePath)
        .expect("save particles");
    auto particles =
        BakedParticlePlayback::prepare(ParticleTrack::loadForRecipe(particlePath, recipe).expect("load particles"))
            .expect("prepare particles");
    require(std::filesystem::remove(particlePath));
    std::array<BakedParticleSample, 1> output{};
    particles.seek(0.5).expect("particle seek");
    require(particles.sample(output).expect("particle sample") == 1);
    require(output[0].pose.x == 1 && output[0].emitter == id && output[0].birthSerial == 1);
    particles.seek(1).expect("death seek");
    require(particles.sample(output).expect("death sample") == 0);
    std::cout << "physics-bake-only: collapse/rigid/deform/particle persistence and independent playback PASS\n";
}
}  // namespace
int main() {
    try {
        run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
