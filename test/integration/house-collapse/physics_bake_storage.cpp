#include <atomic>
#include <barrier>
#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <thread>
#include "physics/destruction/CollapseAnimation.h"
#include "physics/destruction/CollapseBakeRecipe.h"
#include "physics/destruction/CollapsePlayback.h"
#include "physics/destruction/FractureGeometry.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
struct Files {
    std::filesystem::path directory;
    Files() {
        std::random_device entropy;
        for (int i = 0; i < 100; ++i) {
            auto candidate = std::filesystem::temp_directory_path() / ("eve-bake-test-" + std::to_string(entropy()));
            if (std::filesystem::create_directory(candidate)) {
                directory = std::move(candidate);
                return;
            }
        }
        throw std::runtime_error("cannot reserve test directory");
    }
    ~Files() {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
};
void word(std::string& bytes, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<char>((value >> (i * 8)) & 255));
}
std::string legacy(unsigned version) {
    std::string bytes = "EVECOLL1";
    word(bytes, version);
    word(bytes, 1);
    word(bytes, 2);
    if (version == 2) word(bytes, 0);
    for (unsigned frame = 0; frame < 2; ++frame) {
        word(bytes, std::bit_cast<std::uint32_t>(float(frame)));
        for (float value : {0.5f, 0.5f, 0.5f, float(frame) * 10.f, 0.f, 0.f, 0.f, 0.f, 0.f, 1.f})
            word(bytes, std::bit_cast<std::uint32_t>(value));
        word(bytes, frame);
        word(bytes, 0);
        word(bytes, 0);
    }
    return bytes;
}
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    out.close();
    if (!out) throw std::runtime_error("fixture write failed");
}
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
eve::physics::CollapseAnimation fixture(Files& files) {
    const auto path = files.directory / "legacy";
    write(path, legacy(2));
    return eve::physics::CollapseAnimation::load(path.string()).expect("load legacy fixture");
}
}  // namespace

TEST_CASE("physics.bake.legacyMigrationAndSolverFreeSampling") {
    Files files;
    for (unsigned version : {1u, 2u}) {
        const auto old      = files.directory / ("old" + std::to_string(version));
        const auto upgraded = files.directory / ("new" + std::to_string(version));
        write(old, legacy(version));
        auto clip = eve::physics::CollapseAnimation::load(old.string()).expect("legacy");
        REQUIRE(clip.save(upgraded.string()).ok());
        const auto bytes = read(upgraded);
        REQUIRE(bytes[8] == 3);
        REQUIRE(bytes.substr(bytes.size() - 32) ==
                eve::physics::CollapseAnimation::contentKey(std::string_view(bytes).substr(0, bytes.size() - 32)));
        auto loaded = eve::physics::CollapseAnimation::load(upgraded.string()).expect("v3");
        REQUIRE(loaded.sample(0.25f, 0).expect("sample").worldX == 2.5f);
        REQUIRE(loaded.sample(0.99f, 0).expect("before boundary").state == eve::physics::BoneRuntimeState::Attached);
        REQUIRE(loaded.sample(1.f, 0).expect("at boundary").state == eve::physics::BoneRuntimeState::Detached);
        REQUIRE(loaded.sample(-10.f, 0).expect("start").worldX == 0.f);
        REQUIRE(loaded.sample(10.f, 0).expect("end").worldX == 10.f);
    }
}

TEST_CASE("physics.bake.checksumRejectsValidNumericCorruption") {
    Files      files;
    auto       clip = fixture(files);
    const auto path = files.directory / "clip";
    REQUIRE(clip.save(path.string()).ok());
    auto bytes = read(path);
    // A finite position change would pass the old structural validation.
    bytes[40] ^= 1;
    write(path, bytes);
    REQUIRE(!eve::physics::CollapseAnimation::load(path.string()).ok());
    bytes.pop_back();
    write(path, bytes);
    REQUIRE(!eve::physics::CollapseAnimation::load(path.string()).ok());
    bytes    = legacy(2);
    bytes[8] = 99;
    write(path, bytes);
    REQUIRE(!eve::physics::CollapseAnimation::load(path.string()).ok());
}

TEST_CASE("physics.bake.embeddedGeometrySurvivesUpgrade") {
    Files files;
    auto  bytes = legacy(2);
    bytes[20]   = 1;
    std::string mesh;
    word(mesh, 4);
    word(mesh, 4);
    for (float v : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f})
        word(mesh, std::bit_cast<std::uint32_t>(v));
    for (unsigned i : {0u, 2u, 1u, 0u, 1u, 3u, 0u, 3u, 2u, 1u, 2u, 3u}) word(mesh, i);
    for (unsigned tag : {0u, 1u, 0u, 1u}) word(mesh, tag);
    bytes.insert(24, mesh);
    const auto old      = files.directory / "tetra-v2";
    const auto upgraded = files.directory / "tetra-v3";
    write(old, bytes);
    auto clip = eve::physics::CollapseAnimation::load(old.string()).expect("tetra v2");
    REQUIRE(clip.save(upgraded.string()).ok());
    auto       loaded   = eve::physics::CollapseAnimation::load(upgraded.string()).expect("tetra v3");
    const auto geometry = loaded.fragmentGeometry();
    REQUIRE(geometry != nullptr);
    REQUIRE(geometry->fragments.size() == 1);
    REQUIRE(geometry->fragments[0].positions == clip.fragmentGeometry()->fragments[0].positions);
    REQUIRE(geometry->fragments[0].indices == clip.fragmentGeometry()->fragments[0].indices);
    REQUIRE(geometry->fragments[0].materials == clip.fragmentGeometry()->fragments[0].materials);
    loaded = eve::physics::CollapseAnimation{};
    REQUIRE(geometry->validate().ok());
}

TEST_CASE("physics.bake.concurrentPublicationHasOneWinner") {
    Files            files;
    const auto       clip = fixture(files);
    const auto       path = files.directory / "race";
    std::barrier     start(3);
    std::atomic<int> successes{0};
    auto             save = [&] {
        start.arrive_and_wait();
        if (clip.save(path.string()).ok()) ++successes;
    };
    std::thread a(save), b(save);
    start.arrive_and_wait();
    a.join();
    b.join();
    REQUIRE(successes.load() == 1);
    REQUIRE(eve::physics::CollapseAnimation::load(path.string()).ok());
    for (const auto& item : std::filesystem::directory_iterator(files.directory)) REQUIRE(!item.is_directory());
}

TEST_CASE("physics.bake.failedPublicationPreservesExistingBytes") {
    Files      files;
    const auto clip   = fixture(files);
    const auto target = files.directory / "existing";
    write(target, "keep these bytes");
    auto result = clip.save(target.string());
    REQUIRE(!result.ok());
    REQUIRE(result.status().primaryDiagnostic()->code() == eve::DiagnosticCode::Conflict);
    REQUIRE(read(target) == "keep these bytes");
    REQUIRE(!clip.save((files.directory / "missing" / "clip").string()).ok());
    std::string nulPath = target.string();
    nulPath.push_back('\0');
    nulPath += "suffix";
    REQUIRE(!clip.save(nulPath).ok());
    REQUIRE(!eve::physics::CollapseAnimation::load(nulPath).ok());
    for (const auto& item : std::filesystem::directory_iterator(files.directory)) REQUIRE(!item.is_directory());
}

TEST_CASE("physics.bake.independentPlaybackClocks") {
    Files files;
    auto  source = fixture(files);
    auto  first  = eve::physics::CollapsePlayback::prepare(source).expect("freeze");
    auto  second = first;
    source       = {};
    ASSERT(first.animation() == second.animation());
    first.advance(0.25).expect("advance");
    ASSERT(first.sample(0).expect("sample").worldX == 2.5f);
    ASSERT(second.time() == 0.0);
    first.setPaused(true);
    first.advance(10).expect("paused advance");
    ASSERT(first.time() == 0.25);
    first.seek(1).expect("seek while paused");
    first.setRate(-2).expect("reverse");
    first.setPaused(false);
    first.advance(0.25).expect("reverse advance");
    ASSERT(first.time() == 0.5);
    first.setWrap(eve::physics::CollapsePlaybackWrap::Loop).expect("loop");
    first.advance(0.5).expect("reverse wrap");
    ASSERT(first.time() == 0.5);
    first.advance(0.25).expect("exact boundary");
    ASSERT(first.time() == 0.0);
    auto bad = first.advance(-1);
    ASSERT(!bad.ok());
    auto invalid = first.seek(std::numeric_limits<double>::infinity());
    ASSERT(!invalid.ok());
    ASSERT(first.time() == 0.0);
    first.setRate(std::numeric_limits<double>::max()).expect("finite rate");
    auto overflow = first.advance(2);
    ASSERT(!overflow.ok());
    ASSERT(first.time() == 0.0);
    auto empty = eve::physics::CollapsePlayback::prepare({});
    ASSERT(!empty.ok());
}

TEST_CASE("physics.bake.placedSamplesAndZeroDuration") {
    Files files;
    auto  first = eve::physics::CollapsePlayback::prepare(fixture(files)).expect("freeze");
    first.seek(0.5).expect("seek");
    auto second = first;
    // Deliberately nonunit quaternion: admission normalizes a 90-degree Z turn.
    first.setPlacement({1, 2, 3, 0, 0, 2, 2}).expect("place");
    const auto pose = first.sample(0).expect("placed sample");
    ASSERT(std::abs(pose.worldX - 1.f) < 1e-5f);
    ASSERT(std::abs(pose.worldY - 7.f) < 1e-5f);
    ASSERT(pose.worldZ == 3.f);
    ASSERT(std::abs(pose.rotZ - std::sqrt(0.5f)) < 1e-5f);
    ASSERT(std::abs(pose.rotW - std::sqrt(0.5f)) < 1e-5f);
    ASSERT(second.sample(0).expect("independent placement").worldX == 5.f);
    auto zeroQuaternion = first.setPlacement({0, 0, 0, 0, 0, 0, 0});
    ASSERT(!zeroQuaternion.ok());
    auto nanPlacement = first.setPlacement({std::numeric_limits<float>::quiet_NaN(), 0, 0});
    ASSERT(!nanPlacement.ok());
    ASSERT(first.placement().x == 1.f);
    auto invalidMode = first.setWrap(static_cast<eve::physics::CollapsePlaybackWrap>(99));
    ASSERT(!invalidMode.ok());
    first.seek(-2).expect("clamp negative seek");
    ASSERT(first.time() == 0);
    first.seek(2).expect("clamp positive seek");
    ASSERT(first.time() == 1);
    first.setWrap(eve::physics::CollapsePlaybackWrap::Loop).expect("loop");
    first.advance(0).expect("zero delta preserves explicit endpoint");
    ASSERT(first.time() == 1);
    first.advance(2.25).expect("multiple loops");
    ASSERT(first.time() == 0.25);
    auto oneFrame = legacy(2);
    oneFrame[16]  = 1;
    oneFrame.resize(80);
    const auto path = files.directory / "one-frame";
    write(path, oneFrame);
    auto zero = eve::physics::CollapsePlayback::prepare(
                    eve::physics::CollapseAnimation::load(path.string()).expect("single frame"))
                    .expect("zero duration");
    zero.setWrap(eve::physics::CollapsePlaybackWrap::Loop).expect("zero loop");
    zero.setRate(-2).expect("zero reverse");
    zero.advance(10).expect("zero advance");
    ASSERT(zero.time() == 0);
    ASSERT(zero.sample(0).expect("zero sample").worldX == 0.f);
    // Valid finite source coordinates can overflow only after instance placement.
    std::string maximum;
    word(maximum, std::bit_cast<std::uint32_t>(std::numeric_limits<float>::max()));
    oneFrame.replace(40, 4, maximum);
    write(path, oneFrame);
    auto huge = eve::physics::CollapsePlayback::prepare(
                    eve::physics::CollapseAnimation::load(path.string()).expect("large finite source"))
                    .expect("large player");
    huge.setPlacement({std::numeric_limits<float>::max(), 0, 0}).expect("finite placement");
    auto overflow = huge.sample(0);
    ASSERT(!overflow.ok());
    ASSERT(huge.time() == 0);
    huge.setPlacement({}).expect("restore placement");
    ASSERT(huge.sample(0).expect("recovered sample").worldX == std::numeric_limits<float>::max());
}

TEST_CASE("physics.bake.recipeIdentityAndAdmission") {
    using eve::physics::CollapseAnimation;
    using eve::physics::CollapseBakeRecipe;
    CollapseBakeRecipe recipe;
    ASSERT(!recipe.encode().ok());
    recipe.setProducer("collapse-test-v1").expect("producer");
    recipe.setBackend("box3d-test-v1").expect("backend");
    recipe.setParameters("gravity=9.8").expect("parameters");
    recipe.setTiming(0.5f, 2).expect("timing");
    recipe.setSource("house", "asset bytes").expect("source");
    recipe.setSource("material", "wood").expect("dependency");
    recipe.setRandomStream("fracture", 42).expect("rng");
    const auto         encoded = recipe.encode().expect("encode");
    const auto         key     = recipe.key().expect("key");
    CollapseBakeRecipe reordered;
    reordered.setSource("material", "wood").expect("reverse dependency order");
    reordered.setSource("house", "asset bytes").expect("reverse dependency order");
    reordered.setRandomStream("fracture", 42).expect("rng");
    reordered.setParameters("gravity=9.8").expect("parameters");
    reordered.setTiming(0.5f, 2).expect("timing");
    reordered.setBackend("box3d-test-v1").expect("backend");
    reordered.setProducer("collapse-test-v1").expect("producer");
    ASSERT(reordered.encode().expect("sorted canonical bytes") == encoded);
    auto bounded = recipe;
    for (int i = 0; i < 62; ++i)
        bounded.setSource("dependency-" + std::to_string(i), "bytes").expect("fill source budget");
    const auto beforeLimit = bounded.encode().expect("bounded recipe");
    ASSERT(!bounded.setSource("too-many", "bytes").ok());
    ASSERT(bounded.encode().expect("limit failure preserves entries") == beforeLimit);
    bounded.setSource("house", "updated").expect("replace within full budget");
    ASSERT(bounded.encode().expect("replacement changes metadata") != beforeLimit);
    auto maximalSeed = recipe;
    maximalSeed.setRandomStream("fracture", std::numeric_limits<std::uint64_t>::max()).expect("full seed");
    const auto seedBytes = maximalSeed.encode().expect("seed encoding");
    ASSERT(CollapseBakeRecipe::decode(seedBytes).expect("seed decode").encode().expect("seed roundtrip") == seedBytes);
    ASSERT(CollapseBakeRecipe::decode(encoded).expect("decode").encode().expect("canonical") == encoded);
    ASSERT(!CollapseBakeRecipe::decode(encoded + "x").ok());
    ASSERT(!CollapseBakeRecipe::decode(encoded.substr(0, encoded.size() - 1)).ok());
    auto unknown = encoded;
    unknown[7]   = '2';
    ASSERT(!CollapseBakeRecipe::decode(unknown).ok());
    auto planar = recipe;
    ASSERT(planar.setCoordinates(eve::physics::BakeCoordinates::PlanarPixels).ok());
    const auto planarBytes = planar.encode().expect("pixel recipe");
    ASSERT(planarBytes.substr(0, 8) == "EVEBREC1");
    ASSERT(planar.key().expect("pixel key") != key);
    ASSERT(eve::physics::BakeRecipe::decode(planarBytes).expect("pixel decode").coordinates() ==
           eve::physics::BakeCoordinates::PlanarPixels);
    ASSERT(!planar.setCoordinates(static_cast<eve::physics::BakeCoordinates>(99)).ok());
    ASSERT(planar.encode().expect("failed admission preserved") == planarBytes);
    CollapseAnimation wrongSpace;
    ASSERT(!wrongSpace.beginBake(planar).ok());
    ASSERT(!wrongSpace.recipe());
    ASSERT(planar.setCoordinates(eve::physics::BakeCoordinates::MetresRightHandedYUp).ok());
    ASSERT(planar.encode().expect("original encoding restored") == encoded);
    auto changed = recipe;
    changed.setBackend("jolt-test-v1").expect("backend change");
    ASSERT(changed.key().expect("changed key") != key);
    changed = recipe;
    changed.setProducer("collapse-test-v2").expect("algorithm change");
    ASSERT(changed.key().expect("changed key") != key);
    changed = recipe;
    changed.setSource("material", "stone").expect("dependency change");
    ASSERT(changed.key().expect("changed key") != key);
    changed = recipe;
    changed.setRandomStream("fracture", 43).expect("seed change");
    ASSERT(changed.key().expect("changed key") != key);
    changed = recipe;
    changed.setParameters("gravity=10").expect("parameter change");
    ASSERT(changed.key().expect("changed key") != key);
    changed = recipe;
    changed.setTiming(0.25f, 4).expect("step change at same sample rate");
    ASSERT(changed.key().expect("changed key") != key);
    ASSERT(!recipe.setTiming(0, 1).ok());
    ASSERT(!recipe.setProducer("invalid revision").ok());
    ASSERT(recipe.key().expect("unchanged") == key);
    CollapseAnimation empty;
    empty.beginBake(recipe).expect("begin");
    recipe.setBackend("later-builder-change").expect("mutate builder");
    ASSERT(empty.recipe()->key().expect("frozen") == key);

    Files files;
    // Construct an independent v4 fixture: framing precedes geometry and samples.
    std::string framed;
    word(framed, static_cast<std::uint32_t>(encoded.size()));
    framed += encoded;
    auto bytes = legacy(2);
    bytes[8]   = 4;
    bytes.insert(24, framed);
    bytes += CollapseAnimation::contentKey(bytes);
    const auto path = files.directory / "identified";
    write(path, bytes);
    auto expected = CollapseBakeRecipe::decode(encoded).expect("expected");
    auto loaded   = CollapseAnimation::loadForRecipe(path.string(), expected).expect("identified load");
    ASSERT(loaded.recipe()->encode().expect("stored") == encoded);
    ASSERT(!loaded.beginBake(changed).ok());
    auto mismatch = CollapseAnimation::loadForRecipe(path.string(), changed);
    ASSERT(!mismatch.ok());
    ASSERT(mismatch.status().primaryDiagnostic()->code() == eve::DiagnosticCode::Conflict);
    const auto roundtrip = files.directory / "identified-copy";
    loaded.save(roundtrip.string()).expect("v4 save");
    ASSERT(read(roundtrip) == bytes);
    auto missing = CollapseAnimation::loadForRecipe((files.directory / "missing").string(), expected);
    ASSERT(!missing.ok());
    ASSERT(missing.status().primaryDiagnostic()->code() == eve::DiagnosticCode::NotFound);
    write(path, legacy(2));
    auto unidentified = CollapseAnimation::loadForRecipe(path.string(), expected);
    ASSERT(!unidentified.ok());
    ASSERT(unidentified.status().primaryDiagnostic()->code() == eve::DiagnosticCode::PreconditionViolation);
    // A valid checksum cannot make an off-grid frame valid for this recipe.
    bytes.resize(bytes.size() - 32);
    const auto  timeOffset = 24 + framed.size() + 56;
    std::string alteredTime;
    word(alteredTime, std::bit_cast<std::uint32_t>(0.75f));
    bytes.replace(timeOffset, 4, alteredTime);
    bytes += CollapseAnimation::contentKey(bytes);
    write(path, bytes);
    ASSERT(!CollapseAnimation::load(path.string()).ok());
}
