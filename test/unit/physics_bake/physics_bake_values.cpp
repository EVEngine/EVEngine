#include <cmath>
#include <limits>
#include <string>
#include "physics/bake/BakeRecipe.h"
#include "physics/bake/BakedParticlePlayback.h"
#include "physics/bake/DeformPlayback.h"
#include "physics/bake/DeformRecording.h"
#include "physics/bake/DeformTopology.h"
#include "physics/bake/DeformTrack.h"
#include "physics/bake/ParticleTrack.h"
#include "physics/bake/RigidPlayback.h"
#include "physics/bake/RigidPoseTrack.h"
#include "physics/bake/RigidRecording.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve;
using namespace eve::physics;
namespace {
PersistentId id(unsigned char value) {
    PersistentId::Bytes bytes{};
    bytes.back() = value;
    return PersistentId(bytes);
}
std::vector<RigidTrackFrame> recording() {
    return {{0, {{id(2), {20, 4, 0, 0, 0, 0, 2}}, {id(1), {0, 2, 0, 0, 0, 0, 1}}}},
            {2, {{id(1), {10, 6, 0, 0, 0, 0, -1}}, {id(2), {40, 8, 0, 0, 0, 2, 0}}}}};
}
}  // namespace
TEST_CASE("physics.bake.rigidTracksKeepStableIdentities") {
    auto frames = recording();
    auto track  = RigidPoseTrack::prepare(RigidTrackSpace::PlanarPixels, frames).expect("prepare");
    frames.clear();
    REQUIRE(track.trackCount() == 2);
    REQUIRE(track.frameCount() == 2);
    REQUIRE(track.trackId(0).expect("id") == id(1));
    REQUIRE(track.trackId(1).expect("id") == id(2));
    REQUIRE(!track.trackId(2).ok());
    REQUIRE(track.sample(1, id(1)).expect("interpolated").x == 5);
    REQUIRE(track.sample(1, id(2)).expect("other identity").x == 30);
    REQUIRE(track.sample(-1, id(1)).expect("clamped").x == 0);
    REQUIRE(track.sample(99, id(1)).expect("clamped").x == 10);
    auto pose = track.sample(1, id(1)).expect("antipodal rotation");
    REQUIRE(std::abs(pose.qw) > 0.999f);
    auto copy = track;
    track     = RigidPoseTrack::prepare(RigidTrackSpace::Metres3D, recording()).expect("replace source");
    REQUIRE(copy.space() == RigidTrackSpace::PlanarPixels);
    REQUIRE(copy.sample(1, id(2)).expect("copy lifetime").x == 30);
    REQUIRE(!copy.sample(std::numeric_limits<float>::quiet_NaN(), id(1)).ok());
    auto missing = copy.sample(0, id(3));
    REQUIRE(!missing.ok());
    REQUIRE(missing.status().code() == StatusCode::NotFound);
    RigidPlayback first(copy);
    auto          second = first;
    REQUIRE(first.seek(1).ok());
    REQUIRE(second.setRate(-1).ok());
    REQUIRE(second.setWrap(BakePlaybackWrap::Loop).ok());
    REQUIRE(second.advance(0.5).ok());
    REQUIRE(first.time() == 1);
    REQUIRE(second.time() == 1.5);
    REQUIRE(first.sample(id(1)).expect("first player").x == 5);
    REQUIRE(second.sample(id(1)).expect("second player").x == 7.5f);
    second.setPaused(true);
    REQUIRE(second.advance(1).ok());
    REQUIRE(second.time() == 1.5);
    REQUIRE(!second.advance(-1).ok());
    REQUIRE(second.time() == 1.5);
    REQUIRE(second.setPlacement({10, 20, 0, 0, 0, 1, 1}).ok());
    const auto placed = second.sample(id(1)).expect("planar placement");
    REQUIRE(std::abs(placed.x - 5.f) < 0.001f);
    REQUIRE(std::abs(placed.y - 27.5f) < 0.001f);
    REQUIRE(first.sample(id(1)).expect("independent placement").x == 5);
    REQUIRE(!second.setPlacement({0, 0, 1}).ok());
    REQUIRE(!second.setPlacement({0, 0, 0, 1, 0, 0, 1}).ok());
    REQUIRE(second.placement().x == 10);
    RigidPlayback spatial(track);
    REQUIRE(spatial.setPlacement({1, 2, 3, 0, 1, 0, 1}).ok());
    REQUIRE(std::abs(spatial.sample(id(2)).expect("spatial rotation").z + 17.f) < 0.001f);
    BakeRigidTransform invalidPoseCheck;
    REQUIRE(!invalidPoseCheck.apply({0, 0, 0, 0, 0, 0, 0}).ok());
}
TEST_CASE("physics.bake.rigidTracksRejectInvalidRecordings") {
    REQUIRE(!RigidPoseTrack::prepare(RigidTrackSpace::Metres3D, {}).ok());
    REQUIRE(!RigidPoseTrack::prepare(static_cast<RigidTrackSpace>(99), recording()).ok());
    for (int mutation = 0; mutation < 8; ++mutation) {
        auto frames = recording();
        switch (mutation) {
            case 0: frames[1].samples[1].id = id(1); break;
            case 1: frames[1].samples[1].id = id(3); break;
            case 2: frames[1].samples.pop_back(); break;
            case 3: frames[0].samples[0].id = PersistentId::nil(); break;
            case 4: frames[1].seconds = 0; break;
            case 5: frames[0].seconds = 1; break;
            case 6: frames[1].samples[0].pose.x = std::numeric_limits<float>::infinity(); break;
            case 7: frames[1].samples[0].pose.qw = 0; break;
        }
        REQUIRE(!RigidPoseTrack::prepare(RigidTrackSpace::Metres3D, std::move(frames)).ok());
    }
    auto frames                 = recording();
    frames[0].samples[0].pose.z = 4;
    REQUIRE(!RigidPoseTrack::prepare(RigidTrackSpace::PlanarPixels, frames).ok());
    auto three = RigidPoseTrack::prepare(RigidTrackSpace::Metres3D, std::move(frames)).expect("3D");
    REQUIRE(three.sample(0, id(2)).expect("3D z").z == 4);
}

TEST_CASE("physics.bake.deformTopologyOwnsConnectivityIdentity") {
    std::vector<std::uint32_t> indices{0, 1, 2, 2, 1, 3};
    std::vector<DeformUV>      uv{{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    auto       topology = DeformTopology::prepare(DeformPrimitive::Triangles, 4, indices, uv).expect("cloth topology");
    auto       copy     = topology;
    const auto key      = std::string(topology.key());
    indices[1]          = 2;
    uv[0].u             = 2;
    REQUIRE(topology.indices()[1] == 1);
    REQUIRE(topology.uv()[0].u == 0);
    topology = DeformTopology::prepare(DeformPrimitive::Segments, 4, {0, 1, 1, 2, 2, 3}, {}).expect("rope");
    REQUIRE(topology.key() != key);
    REQUIRE(copy.key() == key);
    REQUIRE(copy.primitive() == DeformPrimitive::Triangles);
    REQUIRE(copy.vertexCount() == 4);
    auto torn = DeformTopology::prepare(DeformPrimitive::Triangles, 4, {0, 1, 2}, {{0, 0}, {1, 0}, {0, 1}, {1, 1}})
                    .expect("torn cloth");
    REQUIRE(torn.vertexCount() == copy.vertexCount());
    REQUIRE(torn.key() != copy.key());
    auto winding =
        DeformTopology::prepare(DeformPrimitive::Triangles, 4, {0, 2, 1, 2, 1, 3}, {{0, 0}, {1, 0}, {0, 1}, {1, 1}})
            .expect("winding");
    REQUIRE(winding.key() != copy.key());
    auto negativeZero =
        DeformTopology::prepare(DeformPrimitive::Triangles, 4, {0, 1, 2, 2, 1, 3}, {{-0.f, 0}, {1, 0}, {0, 1}, {1, 1}})
            .expect("canonical zero");
    REQUIRE(negativeZero.key() == copy.key());
    auto cut = DeformTopology::prepare(DeformPrimitive::Segments, 4, {0, 1, 2, 3}, {}).expect("cut rope");
    REQUIRE(cut.key() != topology.key());
    REQUIRE(!DeformTopology::prepare(static_cast<DeformPrimitive>(99), 3, {0, 1, 2}, {}).ok());
    REQUIRE(!DeformTopology::prepare(DeformPrimitive::Triangles, 3, {0, 1}, {}).ok());
    REQUIRE(!DeformTopology::prepare(DeformPrimitive::Triangles, 3, {0, 1, 1}, {}).ok());
    REQUIRE(!DeformTopology::prepare(DeformPrimitive::Triangles, 3, {0, 1, 3}, {}).ok());
    REQUIRE(!DeformTopology::prepare(DeformPrimitive::Segments, 0, {0, 1}, {}).ok());
    REQUIRE(!DeformTopology::prepare(DeformPrimitive::Segments, 1048577, {0, 1}, {}).ok());
    REQUIRE(!DeformTopology::prepare(DeformPrimitive::Segments, 2, {0, 1}, {{0, 0}}).ok());
    REQUIRE(!DeformTopology::prepare(DeformPrimitive::Segments, 2, {0, 1},
                                     {{0, 0}, {std::numeric_limits<float>::infinity(), 0}})
                 .ok());
}

TEST_CASE("physics.bake.deformTrackRejectsTopologyDriftAndSamplesWithoutSource") {
    BakeRecipe recipe;
    REQUIRE(recipe.setProducer("deform-test").ok());
    REQUIRE(recipe.setBackend("test-solver").ok());
    REQUIRE(recipe.setParameters("").ok());
    REQUIRE(recipe.setSource("mesh", "triangle").ok());
    REQUIRE(recipe.setTiming(0.5f, 2).ok());
    auto topology = DeformTopology::prepare(DeformPrimitive::Triangles, 3, {0, 1, 2}, {}).expect("topology");
    std::vector<DeformFrame> frames{{0, topology, {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}},
                                    {1, topology, {{0, 0, 2}, {1, 0, 2}, {0, 1, 2}}}};
    auto                     track = DeformTrack::prepare(frames, recipe).expect("prepare");
    for (int mutation = 0; mutation < 6; ++mutation) {
        auto changed = frames;
        switch (mutation) {
            case 0:
                changed[1].topology =
                    DeformTopology::prepare(DeformPrimitive::Triangles, 3, {0, 2, 1}, {}).expect("reversed");
                break;
            case 1: changed[1].positions.pop_back(); break;
            case 2: changed[1].seconds = 0.5f; break;
            case 3: changed[1].positions[0].x = std::numeric_limits<float>::infinity(); break;
            case 4:
                changed[1].topology =
                    DeformTopology::prepare(DeformPrimitive::Segments, 3, {0, 1, 1, 2}, {}).expect("segments");
                break;
            case 5:
                changed[1].topology =
                    DeformTopology::prepare(DeformPrimitive::Triangles, 3, {0, 1, 2}, {{0, 0}, {1, 0}, {0, 1}})
                        .expect("UV changed");
                break;
        }
        REQUIRE(!DeformTrack::prepare(std::move(changed), recipe).ok());
    }
    REQUIRE(recipe.setCoordinates(BakeCoordinates::PlanarPixels).ok());
    REQUIRE(!DeformTrack::prepare(frames, recipe).ok());
    frames.clear();
    REQUIRE(track.recipe()->coordinates() == BakeCoordinates::MetresRightHandedYUp);
    std::vector<DeformPoint> output(3);
    REQUIRE(track.sample(0.5f, output).ok());
    REQUIRE(output[0].z == 1);
    REQUIRE(output[1].x == 1);
    REQUIRE(output[2].y == 1);
    REQUIRE(!track.sample(std::numeric_limits<float>::quiet_NaN(), output).ok());
    REQUIRE(output[0].z == 1);
    REQUIRE(!track.sample(0, std::span<DeformPoint>(output).first(2)).ok());
    REQUIRE(output[0].z == 1);
    REQUIRE(track.sample(-1, output).ok());
    REQUIRE(output[0].z == 0);
    REQUIRE(track.sample(2, output).ok());
    REQUIRE(output[0].z == 2);
}

TEST_CASE("physics.bake.deformPlayersShareClipButKeepAtomicIndependentOutput") {
    BakeRecipe recipe;
    recipe.setProducer("player-test").expect("producer");
    recipe.setBackend("test").expect("backend");
    recipe.setParameters("").expect("parameters");
    recipe.setSource("rope", "two points").expect("source");
    recipe.setTiming(1, 1).expect("timing");
    auto topology = DeformTopology::prepare(DeformPrimitive::Segments, 2, {0, 1}, {}).expect("topology");
    auto track =
        DeformTrack::prepare({{0, topology, {{0, 0, 0}, {1, 0, 0}}}, {1, topology, {{0, 2, 0}, {1, 2, 0}}}}, recipe)
            .expect("clip");
    auto first  = DeformPlayback::prepare(track).expect("first");
    auto second = DeformPlayback::prepare(track).expect("second");
    REQUIRE(first.track().recipe() == second.track().recipe());
    REQUIRE(first.seek(0.5).ok());
    REQUIRE(second.setRate(-1).ok());
    REQUIRE(second.setWrap(BakePlaybackWrap::Loop).ok());
    REQUIRE(second.advance(0.25).ok());
    REQUIRE(second.time() == 0.75);
    REQUIRE(first.time() == 0.5);
    REQUIRE(first.setPlacement({10, 0, 0, 0, 0, 1, 1}).ok());
    std::vector<DeformPoint> output(2);
    REQUIRE(first.sample(output).ok());
    REQUIRE(std::abs(output[0].x - 9.f) < 0.001f);
    REQUIRE(std::abs(output[1].y - 1.f) < 0.001f);
    REQUIRE(second.sample(output).ok());
    REQUIRE(output[0].y == 1.5f);
    second.setPaused(true);
    REQUIRE(second.advance(0.5).ok());
    REQUIRE(second.time() == 0.75);
    REQUIRE(!second.advance(-1).ok());
    auto huge = DeformTrack::prepare({{0, topology, {{0, 0, 0}, {std::numeric_limits<float>::max(), 0, 0}}}}, recipe)
                    .expect("huge");
    auto overflowing = DeformPlayback::prepare(huge).expect("overflow player");
    REQUIRE(overflowing.setPlacement({std::numeric_limits<float>::max(), 0, 0}).ok());
    output = {{7, 8, 9}, {10, 11, 12}};
    REQUIRE(!overflowing.sample(output).ok());
    REQUIRE(output[0].x == 7);
    REQUIRE(output[1].z == 12);
    REQUIRE(overflowing.setPlacement({}).ok());
    REQUIRE(overflowing.sample(output).ok());
    REQUIRE(output[1].x == std::numeric_limits<float>::max());
    recipe.setCoordinates(BakeCoordinates::PlanarPixels).expect("pixels");
    auto planar       = DeformTrack::prepare({{0, topology, {{0, 0, 0}, {1, 0, 0}}}}, recipe).expect("planar");
    auto planarPlayer = DeformPlayback::prepare(planar).expect("planar player");
    REQUIRE(!planarPlayer.setPlacement({0, 0, 1}).ok());
    REQUIRE(planarPlayer.placement().z == 0);
}

TEST_CASE("physics.bake.particleTracksRespectBirthDeathAndIdentity") {
    BakeRecipe recipe;
    recipe.setProducer("particle-track-test").expect("producer");
    recipe.setSource("emitter", "test-sprite").expect("source");
    recipe.setBackend("cpu").expect("backend");
    recipe.setParameters("").expect("parameters");
    recipe.setCoordinates(BakeCoordinates::PlanarPixels).expect("coordinates");
    recipe.setTiming(0.1f, 1).expect("timing");
    BakedParticlePose left, right;
    right.x        = 10;
    right.rotation = 720;
    right.u0       = 0.5f;
    std::vector<ParticleTrajectory> births{{id(1), 2, {{1, right}, {2, right}}}, {id(1), 1, {{0, left}, {1, right}}}};
    auto                            prepared = ParticleTrack::prepare(3, births, recipe);
    REQUIRE(prepared.ok());
    auto                             clip = prepared.value();
    std::vector<BakedParticleSample> output(clip.capacity());
    auto                             count = clip.sample(0.5f, output);
    REQUIRE(count.ok());
    REQUIRE(count.value() == 1);
    REQUIRE(output[0].birthSerial == 1);
    REQUIRE(output[0].pose.x == 5);
    REQUIRE(output[0].pose.rotation == 360);
    REQUIRE(output[0].pose.u0 == 0);
    auto boundary = clip.sample(1, output);
    REQUIRE(boundary.ok());
    REQUIRE(boundary.value() == 1);
    REQUIRE(output[0].birthSerial == 2);
    REQUIRE(output[0].pose.x == 10);
    REQUIRE(clip.sample(2, output).value() == 0);
    REQUIRE(clip.sample(3, output).value() == 0);
    REQUIRE(!clip.sample(0.5f, std::span<BakedParticleSample>(output).first(1)).ok());
    REQUIRE(output[0].birthSerial == 2);
    REQUIRE(!clip.sample(std::numeric_limits<float>::quiet_NaN(), output).ok());
    births[1].keys[1].pose.x = 999;
    REQUIRE(clip.sample(0.5f, output).value() == 1);
    REQUIRE(output[0].pose.x == 5);
    births[0].birthSerial = 1;
    REQUIRE(!ParticleTrack::prepare(3, births, recipe).ok());
    births[0].birthSerial     = 2;
    births[0].keys[1].seconds = 1;
    REQUIRE(!ParticleTrack::prepare(3, births, recipe).ok());
    REQUIRE(ParticleTrack::prepare(0, {}, recipe).ok());
    auto first  = BakedParticlePlayback::prepare(clip).expect("first player");
    auto second = BakedParticlePlayback::prepare(clip).expect("second player");
    first.seek(0.5).expect("seek");
    first.setPlacement({10, 20, 0, 0, 0, 1, 0}).expect("placement");
    REQUIRE(first.sample(output).value() == 1);
    REQUIRE(std::abs(output[0].pose.x - 5) < 0.0001f);
    REQUIRE(std::abs(output[0].pose.y - 20) < 0.0001f);
    REQUIRE(std::abs(output[0].pose.rotation - 540) < 0.0001f);
    REQUIRE(second.time() == 0);
    second.seek(1).expect("second seek");
    second.setRate(-1).expect("reverse");
    second.advance(0.5).expect("advance");
    REQUIRE(second.sample(output).value() == 1);
    REQUIRE(output[0].pose.rotation == 360);
    second.setPaused(true);
    second.advance(1).expect("paused");
    REQUIRE(second.time() == 0.5);
    second.setPaused(false);
    second.setWrap(BakePlaybackWrap::Loop).expect("loop");
    second.advance(1).expect("reverse wrap");
    REQUIRE(second.time() == 2.5);
    REQUIRE(second.sample(output).value() == 0);
    REQUIRE(first.time() == 0.5);
    REQUIRE(!first.setPlacement({0, 0, 1}).ok());
    REQUIRE(first.placement().x == 10);
    first.setPlacement({std::numeric_limits<float>::max(), 0}).expect("large placement");
    first.seek(1).expect("visible second birth");
    // Translation overflow is observable only above a float ULP at this magnitude.
    BakedParticlePose huge;
    huge.x          = std::numeric_limits<float>::max();
    auto hugeClip   = ParticleTrack::prepare(1, {{id(1), 1, {{0, huge}, {1, huge}}}}, recipe).expect("huge clip");
    auto hugePlayer = BakedParticlePlayback::prepare(hugeClip).expect("huge player");
    hugePlayer.setPlacement({std::numeric_limits<float>::max(), 0}).expect("huge placement");
    output[0].birthSerial = 123;
    REQUIRE(!hugePlayer.sample(output).ok());
    REQUIRE(output[0].birthSerial == 123);
}

TEST_CASE("physics.bake.deformRecordingRejectsPartialFramesAndFreezesIndependentClips") {
    BakeRecipe recipe;
    REQUIRE(!DeformRecording::prepare(recipe, 9).ok());
    recipe.setCoordinates(BakeCoordinates::PlanarPixels).expect("coordinates");
    recipe.setProducer("deform-recording-v1").expect("producer");
    recipe.setBackend("fixture").expect("backend");
    recipe.setSource("mesh", "triangle").expect("source");
    recipe.setParameters("").expect("parameters");
    recipe.setTiming(0.25f, 1).expect("timing");
    const auto key = recipe.key().expect("key");
    REQUIRE(!DeformRecording::prepare(recipe, 0).ok());
    REQUIRE(!DeformRecording::prepare(recipe, 2000001).ok());
    auto recording = DeformRecording::prepare(recipe, 9).expect("prepare");
    recipe.setSource("mesh", "changed after admission").expect("mutate recipe");
    REQUIRE(!recording.freeze().ok());
    REQUIRE(recording.frameCount() == 0);
    REQUIRE(recording.sampleCount() == 0);
    auto        topology = DeformTopology::prepare(DeformPrimitive::Triangles, 3, {0, 1, 2}, {}).expect("topology");
    DeformFrame frame{0, topology, {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}};
    REQUIRE(recording.append(frame).ok());
    frame.positions[0].x = 4;
    auto first           = recording.freeze().expect("first freeze");
    REQUIRE(first.recipe()->key().expect("frozen key") == key);
    std::vector<DeformPoint> output(3);
    first.sample(0, output).expect("sample");
    REQUIRE(output[0].x == 0);
    auto rejected    = frame;
    rejected.seconds = 0.1f;
    REQUIRE(!recording.append(rejected).ok());
    rejected.seconds        = 0.25f;
    rejected.positions[0].x = std::numeric_limits<float>::infinity();
    REQUIRE(!recording.append(rejected).ok());
    rejected                = frame;
    rejected.seconds        = 0.25f;
    rejected.positions[0].z = 1;
    REQUIRE(!recording.append(rejected).ok());
    rejected          = frame;
    rejected.seconds  = 0.25f;
    rejected.topology = DeformTopology::prepare(DeformPrimitive::Triangles, 3, {0, 2, 1}, {}).expect("changed winding");
    REQUIRE(!recording.append(rejected).ok());
    rejected         = frame;
    rejected.seconds = 0.25f;
    rejected.positions.pop_back();
    REQUIRE(!recording.append(rejected).ok());
    REQUIRE(recording.frameCount() == 1);
    REQUIRE(recording.sampleCount() == 3);
    frame.seconds = 0.25f;
    REQUIRE(recording.append(frame).ok());
    REQUIRE(!recording.append(frame).ok());
    auto second = recording.freeze().expect("second freeze");
    REQUIRE(second.frameCount() == 2);
    REQUIRE(first.frameCount() == 1);
    REQUIRE(first.duration() == 0);
    second.sample(0.125f, output).expect("interpolation");
    REQUIRE(output[0].x == 2);
    auto moved           = std::move(recording);
    frame.seconds        = 0.5f;
    frame.positions[0].x = 8;
    REQUIRE(moved.append(frame).ok());
    REQUIRE(moved.sampleCount() == 9);
    frame.seconds = 0.75f;
    REQUIRE(!moved.append(frame).ok());
    REQUIRE(moved.frameCount() == 3);
    auto finalClip = moved.freeze().expect("freeze at budget");
    REQUIRE(finalClip.frameCount() == 3);
    finalClip.sample(0.5f, output).expect("last sample");
    REQUIRE(output[0].x == 8);
    second.sample(0.5f, output).expect("older clip unchanged");
    REQUIRE(output[0].x == 4);
}

TEST_CASE("physics.bake.rigidRecordingPreservesRosterAndRejectsPartialFrames") {
    for (bool planar : {false, true}) {
        BakeRecipe recipe;
        REQUIRE(!RigidRecording::prepare(recipe, 6).ok());
        recipe.setCoordinates(planar ? BakeCoordinates::PlanarPixels : BakeCoordinates::MetresRightHandedYUp)
            .expect("coordinates");
        recipe.setProducer("rigid-recording-v1").expect("producer");
        recipe.setBackend("fixture").expect("backend");
        recipe.setSource("bodies", "pair").expect("source");
        recipe.setParameters("").expect("parameters");
        recipe.setTiming(0.5f, 1).expect("timing");
        const auto key = recipe.key().expect("key");
        REQUIRE(!RigidRecording::prepare(recipe, 0).ok());
        REQUIRE(!RigidRecording::prepare(recipe, 2000001).ok());
        auto recorder = RigidRecording::prepare(recipe, 6).expect("recorder");
        recipe.setSource("bodies", "mutated source").expect("mutate recipe");
        REQUIRE(!recorder.freeze().ok());
        REQUIRE(!recorder.append({0, {}}).ok());
        RigidTrackFrame frame{0, {{id(2), {10, 0, 0, 0, 0, 0, 2}}, {id(1), {0, 0, 0, 0, 0, 0, 2}}}};
        REQUIRE(recorder.append(frame).ok());
        auto first = recorder.freeze().expect("first");
        REQUIRE(first.recipe()->key().expect("frozen recipe") == key);
        REQUIRE(first.sample(0, id(1)).expect("normalized").qw == 1);
        frame.seconds           = 0.5f;
        frame.samples[0].pose.x = 12;
        frame.samples[1].pose.x = 2;
        auto rejected           = frame;
        rejected.seconds        = 0.1f;
        REQUIRE(!recorder.append(rejected).ok());
        rejected               = frame;
        rejected.samples[1].id = id(2);
        REQUIRE(!recorder.append(rejected).ok());
        rejected               = frame;
        rejected.samples[1].id = id(3);
        REQUIRE(!recorder.append(rejected).ok());
        rejected               = frame;
        rejected.samples[1].id = PersistentId::nil();
        REQUIRE(!recorder.append(rejected).ok());
        rejected                    = frame;
        rejected.samples[1].pose.qw = 0;
        REQUIRE(!recorder.append(rejected).ok());
        rejected                   = frame;
        rejected.samples[1].pose.x = std::numeric_limits<float>::quiet_NaN();
        REQUIRE(!recorder.append(rejected).ok());
        rejected = frame;
        rejected.samples.pop_back();
        REQUIRE(!recorder.append(rejected).ok());
        if (planar) {
            rejected                   = frame;
            rejected.samples[1].pose.z = 1;
            REQUIRE(!recorder.append(rejected).ok());
        }
        REQUIRE(recorder.frameCount() == 1);
        REQUIRE(recorder.sampleCount() == 2);
        REQUIRE(recorder.append(frame).ok());
        REQUIRE(!recorder.append(frame).ok());
        auto second = recorder.freeze().expect("second");
        REQUIRE(second.sample(0.25f, id(1)).expect("interpolated").x == 1);
        REQUIRE(first.frameCount() == 1);
        REQUIRE(first.sample(1, id(1)).expect("old clip").x == 0);
        auto moved              = std::move(recorder);
        frame.seconds           = 1;
        frame.samples[0].pose.x = 14;
        frame.samples[1].pose.x = 4;
        REQUIRE(moved.append(frame).ok());
        frame.seconds = 1.5f;
        REQUIRE(!moved.append(frame).ok());
        REQUIRE(moved.sampleCount() == 6);
        auto last = moved.freeze().expect("last");
        REQUIRE(last.frameCount() == 3);
        REQUIRE(last.space() == (planar ? RigidTrackSpace::PlanarPixels : RigidTrackSpace::Metres3D));
        REQUIRE(last.sample(1, id(1)).expect("last pose").x == 4);
        REQUIRE(second.sample(1, id(1)).expect("older snapshot").x == 2);
    }
}
