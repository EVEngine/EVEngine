// clang-format off
/* eve-test-policy:
{
  "schema": "evengine.test-policy",
  "version": 1,
  "origins": [
    "virtual_texture_streaming.cpp"
  ]
}
*/
// clang-format on

#include "graphics/VirtualTextureStreaming.h"

#include "image/ImageData.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

#include <memory>
#include <vector>

using eve::graphics::VirtualTextureBlendLayer;
using eve::graphics::VirtualTextureStreaming;
using eve::graphics::VirtualTextureStreamingConfig;
using eve::image::ImageData;

namespace {

std::shared_ptr<ImageData> solid(float r, float g, float b, float a = 1.f) {
    auto image = std::make_shared<ImageData>(8, 8, "RGBA8");
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) image->setPixel(x, y, {r, g, b, a});
    return image;
}

std::unique_ptr<VirtualTextureStreaming> makeSession(std::size_t capacity = 2, std::size_t maxDecodes = 2) {
    VirtualTextureStreamingConfig config{};
    config.blend.width                = 8;
    config.blend.height               = 8;
    config.blend.tileSize             = 4;
    config.blend.borderSize           = 1;
    config.blend.residentPageCapacity = capacity;
    config.maxDecodesPerUpdate        = maxDecodes;
    auto created                      = VirtualTextureStreaming::create(config);
    REQUIRE(created.ok());
    auto                     session = std::move(created).value();
    VirtualTextureBlendLayer layer{};
    layer.albedo = solid(0.2f, 0.4f, 0.8f);
    REQUIRE(session->addLayer(layer).ok());
    REQUIRE(session->initializePhysicalCache().ok());
    return session;
}

}  // namespace

TEST_CASE("graphics.virtual_texture_streaming.pack_feedback_roundtrip") {
    const auto packed  = VirtualTextureStreaming::packFeedback(3, 5, 2);
    auto       decoded = VirtualTextureStreaming::unpackFeedback(packed);
    REQUIRE(decoded.ok());
    CHECK_EQ(decoded.value().pageX, 3);
    CHECK_EQ(decoded.value().pageY, 5);
    CHECK_EQ(decoded.value().mipLevel, 2);
}

TEST_CASE("graphics.virtual_texture_streaming.feedback_decode_publish_and_abandon") {
    auto session = makeSession();
    REQUIRE(session->recordFeedback(0, 0).ok());
    REQUIRE(session->recordFeedback(1, 0).ok());
    REQUIRE(session->recordFeedback(0, 0).ok());  // dedupe

    auto tick = session->update();
    REQUIRE(tick.ok());
    CHECK(tick.value().pendingUploads > 0);

    auto batch = session->takePendingUploads();
    REQUIRE(batch.ok());
    CHECK(!batch.value().albedoRegions.empty());
    CHECK(!batch.value().pageTableSets.empty());
    // Clears may be empty on a cold cache with free slots.
    REQUIRE(session->commitPublish(batch.value().expectedGeneration).ok());
    CHECK_EQ(session->generation(), 1u);
    CHECK(session->pageTable().getPixel(0, 0).b > 0.95f);
    CHECK(session->pageTable().getPixel(1, 0).b > 0.95f);
    CHECK(session->stats().residentPages == 2u);

    REQUIRE(session->recordFeedback(0, 1).ok());
    auto tick2 = session->update();
    REQUIRE(tick2.ok());
    auto pending = session->takePendingUploads();
    REQUIRE(pending.ok());
    REQUIRE(session->abandonPublish().ok());
    // Abandon restores the previously published mapping.
    CHECK(session->pageTable().getPixel(0, 0).b > 0.95f);
    CHECK(session->stats().abandonedPublishes == 1u);
}

TEST_CASE("graphics.virtual_texture_streaming.evicts_lru_and_clears_page_table_first") {
    auto session = makeSession(/*capacity=*/1, /*maxDecodes=*/1);
    REQUIRE(session->recordFeedback(0, 0).ok());
    REQUIRE(session->update().ok());
    auto first = session->takePendingUploads();
    REQUIRE(first.ok());
    REQUIRE(session->commitPublish(first.value().expectedGeneration).ok());
    CHECK(session->pageTable().getPixel(0, 0).b > 0.95f);

    REQUIRE(session->recordFeedback(1, 0).ok());
    REQUIRE(session->update().ok());
    auto second = session->takePendingUploads();
    REQUIRE(second.ok());
    CHECK(!second.value().pageTableClears.empty());
    REQUIRE(session->commitPublish(second.value().expectedGeneration).ok());
    CHECK(session->pageTable().getPixel(0, 0).b < 0.05f);
    CHECK(session->pageTable().getPixel(1, 0).b > 0.95f);
    CHECK(session->stats().pagesEvicted >= 1u);
}

TEST_CASE("graphics.virtual_texture_streaming.writer_invalidates_dirty_pages") {
    auto session = makeSession();
    REQUIRE(session->recordFeedback(0, 0).ok());
    REQUIRE(session->update().ok());
    auto batch = session->takePendingUploads();
    REQUIRE(batch.ok());
    REQUIRE(session->commitPublish(batch.value().expectedGeneration).ok());

    const auto before = session->stats().pagesDecoded;
    REQUIRE(session->invalidatePage(0, 0).ok());
    REQUIRE(session->update().ok());
    auto dirty = session->takePendingUploads();
    REQUIRE(dirty.ok());
    CHECK(!dirty.value().albedoRegions.empty());
    REQUIRE(session->commitPublish(dirty.value().expectedGeneration).ok());
    CHECK(session->stats().pagesDecoded > before);
}

TEST_CASE("graphics.virtual_texture_streaming.ingest_packed_feedback_collapses_mips") {
    auto session = makeSession();
    // mip=1 page (0,0) collapses to mip0 parent (0,0).
    const std::uint32_t packed = VirtualTextureStreaming::packFeedback(0, 0, 1);
    REQUIRE(session->ingestPackedFeedback(std::span<const std::uint32_t>(&packed, 1)).ok());
    REQUIRE(session->update().ok());
    auto batch = session->takePendingUploads();
    REQUIRE(batch.ok());
    REQUIRE(session->commitPublish(batch.value().expectedGeneration).ok());
    CHECK(session->pageTable().getPixel(0, 0).b > 0.95f);
}
