#include <bit>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include "physics/bake/BakeRecipe.h"
#include "physics/bake/BakedAssetFile.h"
#include "physics/bake/DeformTrack.h"
#include "physics/bake/ParticleTrack.h"
#include "physics/bake/RigidPoseTrack.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"
using namespace eve;
using namespace eve::physics;
namespace {
struct Files {
    std::filesystem::path directory;
    Files() {
        std::random_device random;
        for (int i = 0; i < 100; ++i) {
            auto path = std::filesystem::temp_directory_path() / ("eve-rigid-track-" + std::to_string(random()));
            if (std::filesystem::create_directory(path)) {
                directory = std::move(path);
                return;
            }
        }
        throw std::runtime_error("cannot reserve track test directory");
    }
    ~Files() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};
PersistentId id() {
    PersistentId::Bytes bytes{};
    bytes.back() = 42;
    return PersistentId(bytes);
}
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) throw std::runtime_error("fixture write failed");
}
RigidPoseTrack track(RigidTrackSpace space) {
    return RigidPoseTrack::prepare(space, {{0, {{id(), {1, 2, 0, 0, 0, 0, 1}}}}, {2, {{id(), {5, 6, 0, 0, 0, 0, 1}}}}})
        .expect("prepare");
}
}  // namespace
TEST_CASE("physics.bake.rigidAssetRoundTripAndPublication") {
    Files files;
    for (auto space : {RigidTrackSpace::PlanarPixels, RigidTrackSpace::Metres3D}) {
        const auto path = files.directory / (space == RigidTrackSpace::PlanarPixels ? "planar" : "spatial");
        REQUIRE(track(space).save(path.string()).ok());
        const auto bytes = read(path);
        REQUIRE(bytes.size() == 136);
        REQUIRE(bytes.substr(0, 8) == "EVERIGD1");
        REQUIRE(bytes[8] == 1);
        auto loaded = RigidPoseTrack::load(path.string()).expect("load");
        REQUIRE(loaded.space() == space);
        REQUIRE(loaded.trackId(0).expect("id") == id());
        REQUIRE(loaded.sample(1, id()).expect("sample").x == 3);
        auto duplicate = loaded.save(path.string());
        REQUIRE(!duplicate.ok());
        REQUIRE(duplicate.status().code() == StatusCode::Conflict);
        REQUIRE(read(path) == bytes);
    }
    for (auto space : {RigidTrackSpace::PlanarPixels, RigidTrackSpace::Metres3D}) {
        BakeRecipe recipe;
        REQUIRE(recipe
                    .setCoordinates(space == RigidTrackSpace::PlanarPixels ? BakeCoordinates::PlanarPixels
                                                                           : BakeCoordinates::MetresRightHandedYUp)
                    .ok());
        REQUIRE(recipe.setProducer("rigid-test-v1").ok());
        REQUIRE(recipe.setBackend("backend-test-v1").ok());
        REQUIRE(recipe.setParameters("gravity=0").ok());
        REQUIRE(recipe.setSource("scene", "source-data").ok());
        REQUIRE(recipe.setTiming(1.f, 2).ok());
        const std::vector<RigidTrackFrame> frames = {{0, {{id(), {}}}}, {2, {{id(), {4, 0, 0, 0, 0, 0, 1}}}}};
        auto       prepared  = RigidPoseTrack::prepare(space, frames, recipe).expect("prepare provenance");
        const auto canonical = recipe.encode().expect("canonical");
        auto       changed   = recipe;
        REQUIRE(changed.setBackend("different-backend").ok());
        REQUIRE(prepared.recipe()->encode().expect("frozen recipe") == canonical);
        const auto path = files.directory / (space == RigidTrackSpace::PlanarPixels ? "baked-planar" : "baked-spatial");
        REQUIRE(prepared.save(path.string()).ok());
        const auto bytes = read(path);
        REQUIRE(bytes[8] == 2);
        auto restored = RigidPoseTrack::loadForRecipe(path.string(), recipe).expect("admitted cache");
        REQUIRE(restored.sample(1, id()).expect("restored sample").x == 2);
        REQUIRE(restored.recipe()->encode().expect("restored recipe") == canonical);
        auto mismatch = RigidPoseTrack::loadForRecipe(path.string(), changed);
        REQUIRE(!mismatch.ok());
        REQUIRE(mismatch.status().primaryDiagnostic()->code() == DiagnosticCode::HashMismatch);
        const auto legacy = files.directory / (space == RigidTrackSpace::PlanarPixels ? "planar" : "spatial");
        REQUIRE(!RigidPoseTrack::loadForRecipe(legacy.string(), recipe).ok());
        auto wrongTime       = frames;
        wrongTime[1].seconds = 1;
        REQUIRE(!RigidPoseTrack::prepare(space, std::move(wrongTime), recipe).ok());
        REQUIRE(recipe
                    .setCoordinates(space == RigidTrackSpace::PlanarPixels ? BakeCoordinates::MetresRightHandedYUp
                                                                           : BakeCoordinates::PlanarPixels)
                    .ok());
        REQUIRE(!RigidPoseTrack::prepare(space, frames, recipe).ok());
        // Valid checksum does not admit semantic disagreement between provenance and samples.
        for (int mutation = 0; mutation < 2; ++mutation) {
            auto malformed = bytes.substr(0, bytes.size() - 32);
            if (mutation == 0) {
                malformed[12] = space == RigidTrackSpace::PlanarPixels ? 2 : 1;
            } else {
                const auto secondTimestamp = 28 + canonical.size() + 16 + 32;
                const auto bits            = std::bit_cast<std::uint32_t>(1.f);
                for (unsigned i = 0; i < 4; ++i)
                    malformed[secondTimestamp + i] = static_cast<char>((bits >> (i * 8)) & 255);
            }
            malformed += bakedAssetContentKey(malformed);
            write(path, malformed);
            auto rejected = RigidPoseTrack::load(path.string());
            REQUIRE(!rejected.ok());
            REQUIRE(rejected.status().primaryDiagnostic()->code() == DiagnosticCode::InvalidArgument);
        }
        auto corrupt = bytes;
        corrupt[30] ^= 1;
        write(path, corrupt);
        REQUIRE(!RigidPoseTrack::load(path.string()).ok());
        corrupt     = bytes.substr(0, bytes.size() - 32);
        corrupt[24] = 0;
        corrupt[25] = 0;
        corrupt[26] = 0;
        corrupt[27] = 0;
        corrupt += bakedAssetContentKey(corrupt);
        write(path, corrupt);
        REQUIRE(!RigidPoseTrack::load(path.string()).ok());
    }
    auto missing = RigidPoseTrack::load((files.directory / "missing").string());
    REQUIRE(!missing.ok());
    REQUIRE(missing.status().code() == StatusCode::NotFound);
    REQUIRE(!RigidPoseTrack::load(std::string_view("bad\0path", 8)).ok());
}
TEST_CASE("physics.bake.rigidAssetRejectsCorruptionAndMalformedPayload") {
    Files      files;
    const auto path = files.directory / "clip";
    REQUIRE(track(RigidTrackSpace::PlanarPixels).save(path.string()).ok());
    const auto original = read(path);
    auto       bytes    = original;
    bytes[44] ^= 1;
    write(path, bytes);
    auto corrupted = RigidPoseTrack::load(path.string());
    REQUIRE(!corrupted.ok());
    REQUIRE(corrupted.status().primaryDiagnostic()->code() == DiagnosticCode::HashMismatch);
    for (std::size_t offset : {8u, 12u, 16u, 20u, 39u}) {
        bytes         = original.substr(0, original.size() - 32);
        bytes[offset] = 0;
        bytes += bakedAssetContentKey(bytes);
        write(path, bytes);
        REQUIRE(!RigidPoseTrack::load(path.string()).ok());
    }
    bytes = original.substr(0, original.size() - 32);
    bytes.push_back('x');
    bytes += bakedAssetContentKey(bytes);
    write(path, bytes);
    REQUIRE(!RigidPoseTrack::load(path.string()).ok());
    write(path, original.substr(0, original.size() - 1));
    REQUIRE(!RigidPoseTrack::load(path.string()).ok());
}

TEST_CASE("physics.bake.deformAssetRoundTripAndCheckedRestore") {
    Files      files;
    BakeRecipe recipe;
    REQUIRE(recipe.setProducer("deform-file-test").ok());
    REQUIRE(recipe.setBackend("test").ok());
    REQUIRE(recipe.setParameters("").ok());
    REQUIRE(recipe.setSource("topology", "fixture").ok());
    REQUIRE(recipe.setTiming(1, 1).ok());
    for (auto primitive : {DeformPrimitive::Triangles, DeformPrimitive::Segments}) {
        const auto path = files.directory / (primitive == DeformPrimitive::Triangles ? "cloth" : "rope");
        auto       topology =
            DeformTopology::prepare(primitive, 3,
                                    primitive == DeformPrimitive::Triangles ? std::vector<std::uint32_t>{0, 1, 2}
                                                                            : std::vector<std::uint32_t>{0, 1, 1, 2},
                                    {{0, 0}, {1, 0}, {0, 1}})
                .expect("topology");
        auto clip = DeformTrack::prepare({{0, topology, {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}},
                                          {1, topology, {{0, 2, 0}, {1, 2, 0}, {0, 3, 0}}}},
                                         recipe)
                        .expect("clip");
        REQUIRE(clip.save(path.string()).ok());
        const auto original = read(path);
        auto       loaded   = DeformTrack::loadForRecipe(path.string(), recipe).expect("restored");
        REQUIRE(loaded.topology().key() == topology.key());
        std::vector<DeformPoint> output(3);
        REQUIRE(loaded.sample(0.5f, output).ok());
        REQUIRE(output[2].y == 2);
        REQUIRE(!clip.save(path.string()).ok());
        REQUIRE(read(path) == original);
        auto changed = recipe;
        REQUIRE(changed.setBackend("other").ok());
        REQUIRE(!DeformTrack::loadForRecipe(path.string(), changed).ok());
        auto corrupt = original;
        corrupt[40] ^= 1;
        write(path, corrupt);
        auto broken = DeformTrack::load(path.string());
        REQUIRE(!broken.ok());
        REQUIRE(broken.status().primaryDiagnostic()->code() == DiagnosticCode::HashMismatch);
        for (std::size_t offset : {8u, 12u, 16u, 20u, 28u, 32u}) {
            corrupt = original.substr(0, original.size() - 32);
            for (std::size_t i = 0; i < 4; ++i) corrupt[offset + i] = 0;
            corrupt += bakedAssetContentKey(corrupt);
            write(path, corrupt);
            REQUIRE(!DeformTrack::load(path.string()).ok());
        }
        write(path, original.substr(0, original.size() - 1));
        REQUIRE(!DeformTrack::load(path.string()).ok());
        REQUIRE(loaded.sample(1, output).ok());
        REQUIRE(output[2].y == 3);
    }
    REQUIRE(!DeformTrack::load((files.directory / "missing").string()).ok());
    REQUIRE(!DeformTrack::load(std::string_view("bad\0path", 8)).ok());
}

TEST_CASE("physics.bake.particleAssetRoundTripAndMalformedEvents") {
    Files      files;
    BakeRecipe recipe;
    recipe.setProducer("particle-storage").expect("producer");
    recipe.setBackend("cpu").expect("backend");
    recipe.setSource("emitter", "sprite").expect("source");
    recipe.setParameters("").expect("parameters");
    recipe.setTiming(0.1f, 1).expect("timing");
    recipe.setCoordinates(BakeCoordinates::PlanarPixels).expect("coordinates");
    const auto path = files.directory / "particle.asset";
    {
        BakedParticlePose end;
        end.x       = 20;
        auto source = ParticleTrack::prepare(2, {{id(), UINT64_MAX, {{0, {}}, {1, end}}}}, recipe).expect("prepare");
        REQUIRE(source.save(path.string()).ok());
        REQUIRE(!source.save(path.string()).ok());
    }
    auto                             loaded = ParticleTrack::loadForRecipe(path.string(), recipe).expect("reload");
    std::vector<BakedParticleSample> output(loaded.capacity());
    REQUIRE(loaded.sample(0.5f, output).value() == 1);
    REQUIRE(output[0].birthSerial == UINT64_MAX);
    REQUIRE(output[0].pose.x == 10);
    REQUIRE(loaded.sample(1, output).value() == 0);
    const auto second = files.directory / "second.asset";
    REQUIRE(loaded.save(second.string()).ok());
    const auto original = read(path);
    REQUIRE(read(second) == original);
    recipe.setParameters("changed").expect("parameters");
    REQUIRE(!ParticleTrack::loadForRecipe(path.string(), recipe).ok());
    auto corrupt = original;
    corrupt[20] ^= 1;
    write(second, corrupt);
    REQUIRE(!ParticleTrack::load(second.string()).ok());
    auto resign = [](std::string bytes) {
        bytes.resize(bytes.size() - 32);
        bytes += bakedAssetContentKey(bytes);
        return bytes;
    };
    corrupt    = original;
    corrupt[8] = 2;
    write(second, resign(corrupt));
    REQUIRE(!ParticleTrack::load(second.string()).ok());
    corrupt = original;
    corrupt.insert(corrupt.end() - 32, '\0');
    write(second, resign(corrupt));
    REQUIRE(!ParticleTrack::load(second.string()).ok());
    // A valid checksum must not make a nonincreasing birth/death sequence admissible.
    corrupt              = original;
    const auto deathTime = corrupt.size() - 32 - 56;
    for (std::size_t i = 0; i < 4; ++i) corrupt[deathTime + i] = 0;
    write(second, resign(corrupt));
    REQUIRE(!ParticleTrack::load(second.string()).ok());
    REQUIRE(loaded.sample(0.5f, output).value() == 1);
    REQUIRE(output[0].pose.x == 10);
}
