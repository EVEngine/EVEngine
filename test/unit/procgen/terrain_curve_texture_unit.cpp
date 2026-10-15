#include <zeroerr/unittest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <simplesquirrel/simplesquirrel.hpp>
#include "image/ImageData.h"
#include "procgen/PointSet.h"
#include "procgen/heightmap/Heightmap.h"
#include "procgen/heightmap/TerrainAsset.h"
#include "procgen/heightmap/TerrainCurveTexture.h"
#include "procgen/heightmap/TerrainDerivedMap.h"
#include "procgen/heightmap/TerrainFile.h"
#include "procgen/heightmap/TerrainObjectPlacement.h"
#include "procgen/heightmap/TerrainStamp.h"
#include "procgen/heightmap/TerrainStampScript.h"
#include "procgen/heightmap/TerrainStreaming.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

// terrain_curve_texture.cpp
TEST_CASE("procgen.terrainCurveTexture.preservesPcgFirstPixelAndRangeContract") {
    eve::procgen::Heightmap curve(3, 1);
    curve.setHeight(0, 0, 0.25F);
    curve.setHeight(1, 0, 1.0F);
    curve.setHeight(2, 0, -0.5F);
    eve::image::ImageData output(4, 1, "RGBA32F");
    output.setPixel(0, 0, {1, 1, 1, 1});
    auto baked = eve::procgen::bakeTerrainCurveTexture(output, curve);
    REQUIRE(baked.ok());
    CHECK(baked.value().writtenPixels == 4);
    CHECK(baked.value().minimum == 0.25F);
    CHECK(baked.value().maximum == 1.0F);
    CHECK(output.getPixel(0, 0).r == 0.0F);
    CHECK(output.getPixel(1, 0).r == 0.625F);
    CHECK(output.getPixel(2, 0).r == 1.0F);
    CHECK(output.getPixel(3, 0).r == 0.25F);
    CHECK(output.getPixel(3, 0).a == 0.0F);
}

TEST_CASE("procgen.terrainCurveTexture.failureIsAtomic") {
    eve::procgen::Heightmap curve(2, 1);
    curve.setHeight(0, 0, 0);
    curve.setHeight(1, 0, std::numeric_limits<float>::quiet_NaN());
    eve::image::ImageData output(2, 1, "RGBA32F");
    output.setPixel(1, 0, {0.75F, 0, 0, 1});
    CHECK(!eve::procgen::bakeTerrainCurveTexture(output, curve).ok());
    CHECK(output.getPixel(1, 0).r == 0.75F);
    eve::procgen::Heightmap valid(2, 1);
    eve::image::ImageData   twoRows(2, 2, "RGBA32F");
    CHECK(!eve::procgen::bakeTerrainCurveTexture(twoRows, valid).ok());
    CHECK(twoRows.getWidth() == 2);
    CHECK(twoRows.getHeight() == 2);
}


// terrain_derived_map.cpp
using namespace eve::procgen;

TEST_CASE("procgen.derivedMap.aspectSourceConventions") {
    Heightmap plane(5, 5), output(5, 5);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) plane.setHeight(x, y, float(x) / 4.F);
    REQUIRE(generateTerrainHeightmapAspect(output, plane, TerrainHeightmapAspect::Aspect).ok());
    CHECK(std::abs(output.height(2, 2) - 0.75F) < 0.000001F);
    REQUIRE(generateTerrainHeightmapAspect(output, plane, TerrainHeightmapAspect::Northerness).ok());
    CHECK(std::abs(output.height(2, 2) - 0.5F) < 0.000001F);
    REQUIRE(generateTerrainHeightmapAspect(output, plane, TerrainHeightmapAspect::Easterness).ok());
    CHECK(std::abs(output.height(2, 2)) < 0.000001F);

    Heightmap flat(3, 3);
    flat.data().assign(9, 0.2F);
    REQUIRE(generateTerrainHeightmapAspect(flat, flat, TerrainHeightmapAspect::Aspect).ok());
    CHECK(flat.height(1, 1) == 0.5F);
}

TEST_CASE("procgen.derivedMap.curvatureModesAndAtomicFailure") {
    Heightmap bowl(5, 5), average(5, 5), horizontal(5, 5), vertical(5, 5);
    for (int y = 0; y < 5; ++y)
        for (int x = 0; x < 5; ++x) {
            const float dx = float(x - 2), dy = float(y - 2);
            bowl.setHeight(x, y, 0.02F * (dx * dx + 2.F * dy * dy));
        }
    REQUIRE(generateTerrainHeightmapCurvature(horizontal, bowl, TerrainHeightmapCurvature::Horizontal).ok());
    REQUIRE(generateTerrainHeightmapCurvature(vertical, bowl, TerrainHeightmapCurvature::Vertical).ok());
    REQUIRE(generateTerrainHeightmapCurvature(average, bowl, TerrainHeightmapCurvature::Average).ok());
    CHECK(std::abs(average.height(3, 2) - (horizontal.height(3, 2) + vertical.height(3, 2)) * 0.5F) < 0.000001F);
    CHECK(horizontal.height(3, 2) != vertical.height(3, 2));
    const auto before = average.data();
    CHECK(!generateTerrainHeightmapCurvature(average, bowl, static_cast<TerrainHeightmapCurvature>(9)).ok());
    CHECK(average.data() == before);
    Heightmap wrong(2, 2);
    CHECK(!generateTerrainHeightmapAspect(wrong, bowl, TerrainHeightmapAspect::Aspect).ok());
}


TEST_CASE("procgen.neighborhood.exactInPlaceTraversal") {
    Heightmap grow(2, 2), shrink(2, 2), output(2, 2);
    grow.data() = {0.F, 1.F, 0.F, 0.F};
    auto result = filterTerrainHeightmapNeighborhood(output, grow, 1, TerrainHeightmapNeighborhood::GrowEdges);
    REQUIRE(result.ok());
    const std::vector<float> expectedGrow{0.5F, 1.F, 0.5F, 0.5F};
    CHECK(output.data() == expectedGrow);

    shrink.data() = {0.F, 1.F, 1.F, 1.F};
    result        = filterTerrainHeightmapNeighborhood(output, shrink, 1, TerrainHeightmapNeighborhood::ShrinkEdges);
    REQUIRE(result.ok());
    const std::vector<float> expectedShrink{0.F, 0.5F, 0.5F, 0.5F};
    CHECK(output.data() == expectedShrink);

    Heightmap spike(3, 3);
    spike.data() = {0.F, 0.F, 0.F, 0.F, 1.F, 0.F, 0.F, 0.F, 0.F};
    REQUIRE(filterTerrainHeightmapNeighborhood(spike, spike, 1, TerrainHeightmapNeighborhood::DeNoise).ok());
    CHECK(spike.height(1, 1) == 0.F);
}

TEST_CASE("procgen.neighborhood.invalidInputIsAtomic") {
    Heightmap source(3, 3), target(3, 3);
    source.data().assign(9, 0.25F);
    target.data().assign(9, 0.75F);
    const auto before = target.data();
    CHECK(!filterTerrainHeightmapNeighborhood(target, source, 0, TerrainHeightmapNeighborhood::DeNoise).ok());
    CHECK(target.data() == before);
    CHECK(!filterTerrainHeightmapNeighborhood(target, source, 1, static_cast<TerrainHeightmapNeighborhood>(7)).ok());
    CHECK(target.data() == before);
}

TEST_CASE("procgen.heightmapFilter.sourceOrderAndRadiusQuirk") {
    Heightmap source(2, 2), output(2, 2);
    source.data() = {0.F, 1.F, 0.F, 0.F};
    REQUIRE(smoothTerrainHeightmap(output, source, 1).ok());
    const std::vector<float> expected{0.25F, 0.5625F, 0.0625F, 0.15625F};
    CHECK(output.data() == expected);

    Heightmap wide(11, 12), radiusOutput(11, 12);
    wide.data().assign(132, 1.F);
    REQUIRE(smoothTerrainHeightmapRadius(radiusOutput, wide, 0).ok());
    CHECK(radiusOutput.height(5, 5) == 1.F);
    CHECK(std::abs(radiusOutput.height(5, 6) - 1.F) < 0.00001F);
}


TEST_CASE("procgen.heightmapSlope.exactPlaneAndEdges") {
    Heightmap plane(3, 3), slope(3, 3);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x) plane.setHeight(x, y, float(x) * 0.5F);
    REQUIRE(generateTerrainHeightmapSlope(slope, plane).ok());
    CHECK(std::abs(slope.height(1, 1) - float(0.5 / std::sqrt(1.25))) < 0.000001F);
    CHECK(slope.height(0, 1) < slope.height(1, 1));
    CHECK(slope.height(2, 1) < slope.height(1, 1));
}


TEST_CASE("procgen.heightmapArithmetic.scalarAndClamp") {
    Heightmap source(2, 1), output(2, 1);
    source.data() = {0.25F, 0.75F};
    REQUIRE(
        applyTerrainHeightmapScalarArithmetic(output, source, 0.5F, TerrainHeightmapArithmetic::Add, false, 0.F, 0.F)
            .ok());
    const std::vector<float> expectedAdd{0.75F, 1.25F};
    CHECK(output.data() == expectedAdd);
    REQUIRE(applyTerrainHeightmapScalarArithmetic(output, source, 2.F, TerrainHeightmapArithmetic::Multiply, true, 0.2F,
                                                  1.F)
                .ok());
    const std::vector<float> expectedMultiply{0.5F, 1.F};
    CHECK(output.data() == expectedMultiply);
    const auto before = output.data();
    CHECK(
        !applyTerrainHeightmapScalarArithmetic(output, source, 0.F, TerrainHeightmapArithmetic::Divide, false, 0.F, 1.F)
             .ok());
    CHECK(output.data() == before);
}


TEST_CASE("procgen.heightmapCopy.conditionalAndResampled") {
    Heightmap target(4, 1), source(2, 1);
    target.data() = {0.25F, 0.25F, 0.75F, 0.75F};
    source.data() = {0.F, 1.F};
    REQUIRE(copyTerrainHeightmap(target, source, TerrainHeightmapCopy::IfLess).ok());
    const std::vector<float> expectedLess{0.F, 0.25F, 0.75F, 0.75F};
    CHECK(target.data() == expectedLess);
    REQUIRE(copyTerrainHeightmap(target, source, TerrainHeightmapCopy::IfGreater).ok());
    const std::vector<float> expectedGreater{0.F, 0.5F, 1.F, 1.F};
    CHECK(target.data() == expectedGreater);
}


// terrain_file_raw.cpp
using namespace eve::procgen;

TEST_CASE("procgen.terrainFileRaw.byteDepthEndianAndTraversal") {
    const std::array<std::uint8_t, 4> raw8{0, 64, 128, 255};
    auto                              eight = decodeTerrainFile(raw8, TerrainFileFormat::Raw8);
    REQUIRE(eight.ok());
    CHECK(eight.value().format == "raw8");
    CHECK(eight.value().heightmap.height(0, 0) == 0.F);
    CHECK(std::abs(eight.value().heightmap.height(0, 1) - 64.F / 255.F) < 0.000001F);
    CHECK(std::abs(eight.value().heightmap.height(1, 0) - 128.F / 255.F) < 0.000001F);
    CHECK(eight.value().heightmap.height(1, 1) == 1.F);

    const std::array<std::uint8_t, 8> little{0, 0, 0, 64, 0, 128, 255, 255};
    const std::array<std::uint8_t, 8> big{0, 0, 64, 0, 128, 0, 255, 255};
    auto                              le = decodeTerrainFile(little, TerrainFileFormat::Raw16LE);
    auto                              be = decodeTerrainFile(big, TerrainFileFormat::Raw16BE);
    REQUIRE(le.ok());
    REQUIRE(be.ok());
    CHECK(le.value().heightmap.data() == be.value().heightmap.data());
    CHECK(std::abs(le.value().heightmap.height(1, 0) - 32768.F / 65535.F) < 0.000001F);
}

TEST_CASE("procgen.terrainFileRaw.requiresExplicitExactSquareFormat") {
    const std::array<std::uint8_t, 3> malformed{0, 1, 2};
    CHECK(!decodeTerrainFile(malformed, TerrainFileFormat::Raw8).ok());
    CHECK(!decodeTerrainFile(malformed, TerrainFileFormat::Raw16LE).ok());
    const std::array<std::uint8_t, 4> raw{0, 1, 2, 3};
    CHECK(!decodeTerrainFile(raw, TerrainFileFormat::Auto).ok());
    CHECK(int(parseTerrainFileFormat("RAW8")) == int(TerrainFileFormat::Raw8));
    CHECK(int(parseTerrainFileFormat("raw16le")) == int(TerrainFileFormat::Raw16LE));
    CHECK(int(parseTerrainFileFormat("raw16be")) == int(TerrainFileFormat::Raw16BE));
}

// terrain_heightmap_access.cpp
using namespace eve::procgen;

TEST_CASE("procgen.heightmapAccess.safeNormalizedAndShapeQueries") {
    Heightmap map(2, 2);
    map.data() = {0, 1, 2, 3};
    auto safe  = sampleTerrainHeightmapSafe(map, -7, 9);
    REQUIRE(safe.ok());
    CHECK(safe.value() == 2);
    auto middle = sampleTerrainHeightmapNormalized(map, 0.25F, 0.25F);
    REQUIRE(middle.ok());
    CHECK(middle.value() == 1.5F);
    auto edge = sampleTerrainHeightmapNormalized(map, 1, 1);
    REQUIRE(edge.ok());
    CHECK(edge.value() == 3);
    REQUIRE(terrainHeightmapHasData(map).ok());
    CHECK(terrainHeightmapHasData(map).value());
    REQUIRE(terrainHeightmapIsPowerOfTwo(map).ok());
    CHECK(terrainHeightmapIsPowerOfTwo(map).value());
    Heightmap rectangular(2, 3), empty;
    REQUIRE(terrainHeightmapIsPowerOfTwo(rectangular).ok());
    CHECK(!terrainHeightmapIsPowerOfTwo(rectangular).value());
    REQUIRE(terrainHeightmapHasData(empty).ok());
    CHECK(!terrainHeightmapHasData(empty).value());
    CHECK(!sampleTerrainHeightmapNormalized(map, -0.1F, 0).ok());
}


// terrain_object.cpp
using namespace eve::procgen;

namespace {
[[maybe_unused]] Heightmap filled(int width, int height, float value) {
    Heightmap result(width, height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) result.setHeight(x, y, value);
    return result;
}
[[maybe_unused]] TerrainObjectPlacementSettings settings() {
    TerrainObjectPlacementSettings value;
    value.width = value.depth = 2;
    value.spacing             = 1;
    value.spawnDensity        = 1;
    value.jitterPercent = value.failureRate = value.minimumFitness = value.minimumInstanceFitness = 0;
    value.boundsRadius                                                                            = 0.2F;
    value.prototype                                                                               = "rocks";
    value.namespaceId                                                                             = 71;
    value.seed                                                                                    = 5;
    TerrainObjectInstanceSettings instance;
    instance.asset            = "rock-a";
    instance.minimumInstances = instance.maximumInstances = 1;
    value.instances.push_back(instance);
    return value;
}
}  // namespace

TEST_CASE("procgen.terrainObject.exportsStableAttributedTransformsAndRemoval") {
    auto     fitness = filled(3, 3, 1), heights = filled(2, 2, 0.25F);
    auto     config = settings();
    PointSet first, second;
    auto     emitted = exportTerrainObjectPoints(first, fitness, heights, config);
    REQUIRE(emitted.ok());
    CHECK(emitted.value() == 9);
    REQUIRE(exportTerrainObjectPoints(second, fitness, heights, config).ok());
    CHECK(first.getCount() == second.getCount());
    for (int i = 0; i < first.getCount(); ++i) {
        CHECK(first.getPointId(i) == second.getPointId(i));
        CHECK(first.getX(i) == second.getX(i));
        CHECK(first.getY(i) == 0.25F);
        CHECK(first.getStringAttribute(i, "asset", "") == "rock-a");
        CHECK(first.getStringAttribute(i, "objectPrototype", "") == "rocks");
        CHECK(first.hasIntAttribute(i, "objectCandidate"));
        CHECK(first.getIntAttribute(i, "objectResource", -1) == 0);
    }
    PointSet removed;
    auto     count = removeTerrainObjectPoints(removed, first, fitness, config, 0.5F);
    REQUIRE(count.ok());
    CHECK(count.value() == 9);
    CHECK(removed.empty());
}

TEST_CASE("procgen.terrainObject.supportsPrototypeChildrenCollisionAndAtomicFailure") {
    auto fitness = filled(5, 5, 1), heights = filled(3, 3, 0);
    auto config                               = settings();
    config.boundsCollisionCheck               = true;
    config.boundsRadius                       = 0.75F;
    config.instances.front().minimumInstances = config.instances.front().maximumInstances = 2;
    config.instances.front().minimumOffsetX = config.instances.front().maximumOffsetX = -0.1F;
    config.instances.front().minimumOffsetZ = config.instances.front().maximumOffsetZ = 0.1F;
    config.instances.front().scaleMode             = TerrainObjectScaleMode::FitnessRandomized;
    config.instances.front().scaleRandomPercentage = 0.2F;
    config.instances.front().minimumRotationY      = -10;
    config.instances.front().maximumRotationY      = 10;
    PointSet output;
    auto     emitted = exportTerrainObjectPoints(output, fitness, heights, config);
    REQUIRE(emitted.ok());
    CHECK(emitted.value() == 8);
    PointSet preserved = output;
    config.maxPoints   = 1;
    CHECK(!exportTerrainObjectPoints(output, fitness, heights, config).ok());
    CHECK(output.getCount() == preserved.getCount());
    CHECK(output.getPointId(0) == preserved.getPointId(0));
}


TEST_CASE("procgen.terrainObject.multiTileSharesRngCollisionAndOwnsHistory") {
    PointSet empty, west, east;
    auto     heights = filled(2, 2, 0.25F), fitness = filled(4, 2, 1);
    auto     config             = settings();
    config.boundsCollisionCheck = true;
    config.boundsRadius         = 0.6F;
    config.maxPoints            = 100;
    TerrainStampSettings bounds;
    bounds.centerX = 2;
    bounds.centerZ = 1;
    bounds.width   = 4;
    bounds.depth   = 2;
    TerrainMultiObjectWorkspace workspace;
    REQUIRE(workspace.addTile("west", empty, heights, 0, 0, 2, 2, 2, 2).ok());
    REQUIRE(workspace.addTile("east", empty, heights, 2, 0, 2, 2, 2, 2).ok());
    auto added = workspace.apply(fitness, config, bounds, TerrainObjectOperationMode::Add);
    REQUIRE(added.ok());
    REQUIRE(workspace.copyTile("west", west).ok());
    REQUIRE(workspace.copyTile("east", east).ok());
    CHECK(added.value() == west.getCount() + east.getCount());
    CHECK(added.value() > 0);
    for (const auto& a : west.points())
        for (const auto& b : east.points()) CHECK(std::hypot(double(a.x) - b.x, double(a.z) - b.z) >= 1.2 - 1e-5);
    REQUIRE(workspace.undo().ok());
    REQUIRE(workspace.copyTile("west", west).ok());
    CHECK(west.empty());
    REQUIRE(workspace.redo().ok());
    CHECK(workspace.getAppliedCount() == 1);
    REQUIRE(workspace.apply(fitness, config, bounds, TerrainObjectOperationMode::Replace).ok());
    CHECK(!workspace.redo().ok());
}


// terrain_performance.cpp
using namespace eve;
using namespace eve::procgen;

namespace {
constexpr std::size_t kHeaderSize = 44, kEntrySize = 36;
[[maybe_unused]] void                  put16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(std::uint8_t(value));
    out.push_back(std::uint8_t(value >> 8));
}
[[maybe_unused]] void put32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(std::uint8_t(value >> shift));
}
[[maybe_unused]] void put64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) out.push_back(std::uint8_t(value >> shift));
}
[[maybe_unused]] void putFloat(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    put32(out, bits);
}
[[maybe_unused]] std::uint32_t checksum(const std::vector<std::uint8_t>& bytes) {
    std::uint32_t value = 2166136261U;
    for (auto byte : bytes) {
        value ^= byte;
        value *= 16777619U;
    }
    return value;
}
[[maybe_unused]] double milliseconds(std::chrono::steady_clock::duration duration) {
    return std::chrono::duration<double, std::milli>(duration).count();
}

/** Full EVTR address space backed only for metadata and the chunks actually read. */
class SparseTerrainArchiveSource final : public ITerrainArchiveSource {
public:
    explicit SparseTerrainArchiveSource(int dimension) {
        constexpr int chunkSize = 64;
        for (int index = 0; index < chunkSize * chunkSize; ++index) {
            put16(chunk_, std::uint16_t(index));
            chunk_.insert(chunk_.end(), {255, 0, 128, 128, 0, 0, 128, 128, 0, 2});
        }
        const int  columns = (dimension + chunkSize - 1) / chunkSize;
        const auto count   = std::uint32_t(columns * columns);
        payloadStart_      = kHeaderSize + std::uint64_t(count) * kEntrySize;
        metadata_.insert(metadata_.end(), {'E', 'V', 'T', 'R'});
        put16(metadata_, 5);
        put16(metadata_, 0);
        put32(metadata_, dimension);
        put32(metadata_, dimension);
        put32(metadata_, chunkSize);
        put32(metadata_, count);
        putFloat(metadata_, 0);
        putFloat(metadata_, 1);
        putFloat(metadata_, 1);
        put64(metadata_, kHeaderSize);
        const auto    chunkChecksum = checksum(chunk_);
        const auto    chunkBytes    = static_cast<std::uint32_t>(chunk_.size());
        std::uint64_t offset        = payloadStart_;
        for (int y = 0; y < columns; ++y)
            for (int x = 0; x < columns; ++x) {
                put32(metadata_, x);
                put32(metadata_, y);
                put16(metadata_, chunkSize);
                put16(metadata_, chunkSize);
                metadata_.insert(metadata_.end(), {0, 0, 0, 0});
                put64(metadata_, offset);
                put32(metadata_, chunkBytes);
                put32(metadata_, chunkBytes);
                put32(metadata_, chunkChecksum);
                offset += chunk_.size();
            }
        size_ = offset;
    }
    [[nodiscard]] std::uint64_t                     size() const noexcept override { return size_; }
    [[nodiscard]] Result<std::vector<std::uint8_t>> read(std::uint64_t offset, std::size_t length) const override {
        bytesRead_ += length;
        ++reads_;
        if (offset <= metadata_.size() && length <= metadata_.size() - offset) {
            auto begin = metadata_.begin() + std::ptrdiff_t(offset);
            return Result<std::vector<std::uint8_t>>::success({begin, begin + std::ptrdiff_t(length)});
        }
        if (offset >= payloadStart_ && length == chunk_.size() && (offset - payloadStart_) % chunk_.size() == 0 &&
            offset + length <= size_)
            return Result<std::vector<std::uint8_t>>::success(chunk_);
        return Result<std::vector<std::uint8_t>>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                                            "sparse terrain read is out of range", {},
                                                                            {}, "procgen.terrain-performance"));
    }
    [[nodiscard]] std::uint64_t bytesRead() const noexcept { return bytesRead_; }
    [[nodiscard]] std::uint64_t reads() const noexcept { return reads_; }

private:
    std::uint64_t             payloadStart_ = 0, size_ = 0;
    std::vector<std::uint8_t> metadata_, chunk_;
    mutable std::uint64_t     bytesRead_ = 0, reads_ = 0;
};
}  // namespace
