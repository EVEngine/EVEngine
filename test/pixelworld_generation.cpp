#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include "pixelworld/PixelWorldGeneration.h"
#include "pixelworld/PixelWorldGenerationCodec.h"

#include <algorithm>
#include <limits>
#include <set>
#include <string>

using namespace eve::pixelworld;

TEST_CASE("pixelworld_generation.is_bit_exact_seamless_and_seed_sensitive") {
    const MaterialCatalog catalog = MaterialCatalog::builtIn();
    PixelWorldGenerationRequest request;
    request.seed = 0xC0FFEE;
    request.region = {-3, -1, 3, 2};
    request.surfaceY = 4;
    request.terrainAmplitude = 20;
    request.waterLevel = 48;
    request.caveThreshold = 4200;

    const auto first = generatePixelWorld(request, catalog, 1).expect("first generated world");
    const auto second = generatePixelWorld(request, catalog, 1).expect("second generated world");
    CHECK_EQ(first.contentHash, second.contentHash);
    CHECK_EQ(first.batch, second.batch);
    CHECK_EQ(first.chunks, second.chunks);
    CHECK(std::any_of(first.chunks.begin(), first.chunks.end(), [](const auto& chunk) {
        return chunk.caveCells > 0;
    }));
    CHECK(std::any_of(first.chunks.begin(), first.chunks.end(), [](const auto& chunk) {
        return chunk.solidCells > 0;
    }));

    PixelWorldGenerationRequest single = request;
    single.region = {0, 0, 0, 0};
    const auto isolated = generatePixelWorld(single, catalog, 1).expect("isolated Chunk");
    const auto wholeChunk = std::find_if(first.batch.chunks.begin(), first.batch.chunks.end(),
                                         [](const auto& chunk) {
                                             return chunk.x == 0 && chunk.y == 0;
                                         });
    REQUIRE(wholeChunk != first.batch.chunks.end());
    REQUIRE_EQ(isolated.batch.chunks.size(), std::size_t(1));
    CHECK_EQ(wholeChunk->cells, isolated.batch.chunks.front().cells);

    request.seed += 1;
    const auto different = generatePixelWorld(request, catalog, 1).expect("different seed");
    CHECK(different.contentHash != first.contentHash);
}

TEST_CASE("pixelworld_generation.stamps_cross_chunk_boundaries_and_apply_transactionally") {
    const MaterialCatalog catalog = MaterialCatalog::builtIn();
    PixelWorldGenerationRequest request;
    request.seed = 77;
    request.region = {0, 0, 1, 0};
    request.surfaceY = 0;
    request.caveThreshold = 0;
    PixelMaterialStamp stamp;
    stamp.originX = 62;
    stamp.originY = 10;
    stamp.width = 4;
    stamp.height = 2;
    stamp.cells.assign(8, {MaterialId::Wood, 20, 0, 0});
    request.stamps.push_back(stamp);

    const auto generated = generatePixelWorld(request, catalog, 1).expect("stamped generation");
    REQUIRE_EQ(generated.batch.chunks.size(), std::size_t(2));
    CHECK_EQ(generated.chunks[0].stampedCells, std::uint32_t(4));
    CHECK_EQ(generated.chunks[1].stampedCells, std::uint32_t(4));

    PixelWorld world(request.seed, catalog);
    const auto receipt = world.applyChunkBatch(generated.batch, 0).expect("apply generated batch");
    CHECK_EQ(receipt.chunksReplaced, std::uint32_t(2));
    CHECK_EQ(world.revision(), std::uint64_t(1));
    for (int y = 10; y < 12; ++y)
        for (int x = 62; x < 66; ++x)
            CHECK_EQ(world.getMaterial(x, y), int(MaterialId::Wood));

    const auto before = world.saveSnapshot().expect("before invalid generation");
    PixelWorldGenerationRequest invalid = request;
    invalid.schemaVersion = 99;
    CHECK(!generatePixelWorld(invalid, catalog, 2).ok());
    const auto after = world.saveSnapshot().expect("after invalid generation");
    REQUIRE_EQ(after.size(), before.size());
    CHECK(std::equal(after.begin(), after.end(), before.begin()));
}

TEST_CASE("pixelworld_generation.codec_round_trips_and_rejects_unknown_data") {
    PixelWorldGenerationRequest request;
    request.seed = std::numeric_limits<std::uint64_t>::max();
    request.region = {-2, 3, 4, 5};
    request.surfaceY = -17;
    request.terrainAmplitude = 91;
    request.waterLevel = 73;
    request.caveThreshold = 1234;
    PixelMaterialStamp stamp;
    stamp.originX = -65;
    stamp.originY = 7;
    stamp.width = 1;
    stamp.height = 1;
    stamp.cells.push_back({MaterialId::Lava, 777, 9, 4});
    request.stamps.push_back(stamp);

    const std::string encoded =
        encodePixelWorldGenerationRequestJson(request).expect("encode request");
    const auto decoded =
        decodePixelWorldGenerationRequestJson(encoded).expect("decode request");
    CHECK_EQ(decoded.schemaVersion, request.schemaVersion);
    CHECK_EQ(decoded.seed, request.seed);
    CHECK_EQ(decoded.region.minX, request.region.minX);
    CHECK_EQ(decoded.region.minY, request.region.minY);
    CHECK_EQ(decoded.region.maxX, request.region.maxX);
    CHECK_EQ(decoded.region.maxY, request.region.maxY);
    CHECK_EQ(decoded.surfaceY, request.surfaceY);
    CHECK_EQ(decoded.terrainAmplitude, request.terrainAmplitude);
    CHECK_EQ(decoded.waterLevel, request.waterLevel);
    CHECK_EQ(decoded.caveThreshold, request.caveThreshold);
    REQUIRE_EQ(decoded.stamps.size(), std::size_t(1));
    CHECK_EQ(decoded.stamps.front().cells, request.stamps.front().cells);
    CHECK_EQ(encodePixelWorldGenerationRequestJson(decoded).expect("re-encode request"), encoded);

    std::string unknownVersion = encoded;
    const auto versionAt = unknownVersion.find("\"version\": 1");
    REQUIRE(versionAt != std::string::npos);
    unknownVersion.replace(versionAt, std::string("\"version\": 1").size(), "\"version\": 2");
    CHECK(!decodePixelWorldGenerationRequestJson(unknownVersion).ok());

    std::string unknownField = encoded;
    const auto closingBrace = unknownField.rfind('}');
    REQUIRE(closingBrace != std::string::npos);
    unknownField.insert(closingBrace, ",\n  \"future\": true\n");
    CHECK(!decodePixelWorldGenerationRequestJson(unknownField).ok());
    CHECK(!decodePixelWorldGenerationRequestJson("{broken").ok());
}
