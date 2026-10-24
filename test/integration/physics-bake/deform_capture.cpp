#include <cmath>
#include <filesystem>
#include <random>
#include "physics/bake/BakeRecipe.h"
#include "physics/bake/DeformPlayback.h"
#include "physics/bake/DeformRecording.h"
#include "physics/bake/DeformTrack.h"
#include "physics/bake/deform_capture/DeformBakeCapture.h"
#include "physics/cloth/Cloth3D.h"
#include "physics/rope/Rope3D.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve::physics;
namespace {
struct DeformFiles {
    std::filesystem::path directory;
    DeformFiles() {
        std::random_device random;
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto candidate =
                std::filesystem::temp_directory_path() / ("eve-deform-capture-" + std::to_string(random()));
            if (std::filesystem::create_directory(candidate)) {
                directory = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("cannot reserve deformation capture directory");
    }
    ~DeformFiles() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};
BakeRecipe recipe() {
    BakeRecipe value;
    value.setProducer("deform-capture-test").expect("producer");
    value.setBackend("cpu-test").expect("backend");
    value.setParameters("gravity-default").expect("parameters");
    value.setSource("fixture", "three-by-three-cloth-or-four-node-rope").expect("source");
    value.setTiming(1.f / 60.f, 30).expect("timing");
    return value;
}
}  // namespace
TEST_CASE("physics.bake.ropeCaptureRejectsCutTopologyAfterSourceDestruction") {
    std::vector<DeformFrame> frames;
    auto                     recording = DeformRecording::prepare(recipe(), 64).expect("recording");
    {
        Rope3D rope(4, 0, 3, 0, 3, 3, 0);
        frames.push_back(captureDeformFrame(rope, 0).expect("initial rope"));
        REQUIRE(recording.append(frames[0]).ok());
        for (int i = 0; i < 30; ++i) rope.update(1.f / 60.f);
        frames.push_back(captureDeformFrame(rope, 0.5f).expect("moving rope"));
        REQUIRE(frames[1].positions[0].y < frames[0].positions[0].y);
        REQUIRE(rope.cut(1).ok());
        auto cut = captureDeformFrame(rope, 0.5f).expect("cut rope");
        REQUIRE(cut.topology.vertexCount() == frames[0].topology.vertexCount());
        REQUIRE(cut.topology.indices().size() == 4);
        REQUIRE(cut.topology.indices()[2] == 2);
        REQUIRE(!DeformTrack::prepare({frames[0], cut}, recipe()).ok());
        REQUIRE(!recording.append(cut).ok());
        REQUIRE(recording.frameCount() == 1);
        REQUIRE(recording.append(frames[1]).ok());
        REQUIRE(!captureDeformFrame(rope, -1).ok());
        REQUIRE(rope.cut(0).ok());
        REQUIRE(rope.cut(2).ok());
        REQUIRE(!captureDeformFrame(rope, 0.5f).ok());
        REQUIRE(rope.repair(0).ok());
        REQUIRE(rope.repair(1).ok());
        REQUIRE(rope.repair(2).ok());
        auto repaired = captureDeformFrame(rope, 0.5f).expect("repaired connectivity");
        REQUIRE(repaired.topology.key() == frames[0].topology.key());
        REQUIRE(DeformTrack::prepare({frames[0], repaired}, recipe()).ok());
    }
    auto        track = recording.freeze().expect("frozen rope");
    DeformFiles files;
    const auto  path = (files.directory / "captured.deform").string();
    REQUIRE(track.save(path).ok());
    auto loaded   = DeformTrack::loadForRecipe(path, recipe()).expect("reload captured deformation");
    auto playback = DeformPlayback::prepare(loaded).expect("prepare playback");
    REQUIRE(playback.seek(0.25).ok());
    std::vector<DeformPoint> positions(loaded.topology().vertexCount());
    REQUIRE(playback.sample(positions).ok());
    REQUIRE(positions[0].y < 3);
    REQUIRE(std::isfinite(positions[3].x));
}
TEST_CASE("physics.bake.clothCaptureKeepsActualSurvivingTriangles") {
    std::vector<DeformFrame> frames;
    auto                     recording = DeformRecording::prepare(recipe(), 64).expect("recording");
    {
        Cloth3D cloth(3, 3, 1.f, 0.f, 3.f, 0.f);
        frames.push_back(captureDeformFrame(cloth, 0).expect("initial cloth"));
        REQUIRE(recording.append(frames[0]).ok());
        for (int i = 0; i < 30; ++i) cloth.update(1.f / 60.f);
        frames.push_back(captureDeformFrame(cloth, 0.5f).expect("moving cloth"));
        const auto count = frames[0].topology.indices().size();
        cloth.tearConstraint(0, 1);
        auto torn = captureDeformFrame(cloth, 0.5f).expect("torn cloth");
        REQUIRE(torn.topology.vertexCount() == frames[0].topology.vertexCount());
        REQUIRE(torn.topology.indices().size() < count);
        REQUIRE(torn.topology.indices().size() == static_cast<std::size_t>(cloth.getTriangleCount()) * 3);
        REQUIRE(!DeformTrack::prepare({frames[0], torn}, recipe()).ok());
        REQUIRE(!recording.append(torn).ok());
        REQUIRE(recording.frameCount() == 1);
        REQUIRE(recording.append(frames[1]).ok());
    }
    auto        track = recording.freeze().expect("frozen cloth");
    DeformFiles files;
    const auto  path = (files.directory / "captured.deform").string();
    REQUIRE(track.save(path).ok());
    auto loaded   = DeformTrack::loadForRecipe(path, recipe()).expect("reload captured deformation");
    auto playback = DeformPlayback::prepare(loaded).expect("prepare playback");
    REQUIRE(playback.seek(0.25).ok());
    std::vector<DeformPoint> positions(loaded.topology().vertexCount());
    REQUIRE(playback.sample(positions).ok());
    REQUIRE(positions.size() == 9);
    for (auto point : positions) REQUIRE(std::isfinite(point.y));
}
