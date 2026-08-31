#include "pixelworld_streaming/PixelWorldStreaming.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace eve::pixelworld_streaming {
namespace {

using Coord = std::pair<int, int>;

Coord coord(const eve::pixelworld::PixelChunkSnapshot& chunk) { return {chunk.y, chunk.x}; }

bool contains(eve::pixelworld::PixelChunkRegion region, Coord value) noexcept {
    const auto [y, x] = value;
    return x >= region.minX && x <= region.maxX && y >= region.minY && y <= region.maxY;
}

}  // namespace

eve::Result<PixelChunkStreamUpdate> PixelChunkStreamCursor::capture(
    const eve::pixelworld::PixelWorld& source, eve::pixelworld::PixelChunkRegion interest) {
    auto all = source.snapshotChunksInRegion(interest, 0);
    if (!all) return eve::Result<PixelChunkStreamUpdate>::failure(all.status());

    const auto link = source.worldLink();
    const bool fullResync = source_.world == 0 || source_ != link;
    std::set<Coord> nextKnown;
    std::set<Coord> emitted;
    std::vector<eve::pixelworld::PixelChunkSnapshot> output;
    output.reserve(all.value().size() + known_.size());
    PixelChunkStreamUpdate update;
    update.fullResync = fullResync;

    for (const auto& chunk : all.value()) {
        const Coord key = coord(chunk);
        if (chunk.removed) {
            if (fullResync || known_.contains(key)) {
                output.push_back(chunk);
                emitted.insert(key);
            }
            continue;
        }
        nextKnown.insert(key);
        if (fullResync || !known_.contains(key) || chunk.revision > revision_) {
            output.push_back(chunk);
            emitted.insert(key);
            if (!known_.contains(key)) ++update.chunksEntered;
        }
    }

    for (const Coord& key : known_) {
        if (nextKnown.contains(key) || emitted.contains(key)) continue;
        ++update.chunksEvicted;
        if (fullResync || contains(interest, key)) continue;
        const auto [y, x] = key;
        output.push_back({x, y, source.revision(), true, {}});
    }
    std::sort(output.begin(), output.end(), [](const auto& left, const auto& right) {
        return coord(left) < coord(right);
    });

    update.batch.catalogFingerprint = source.materialCatalogFingerprint();
    update.batch.sourceSeed = source.seed();
    update.batch.sourceRevision = source.revision();
    update.batch.sourceTick = eve::SimulationTick(source.tickValue());
    update.batch.sourceLastEditSequence = source.lastEditSequence();
    update.batch.fullResync = fullResync;
    update.batch.chunks = std::move(output);

    source_ = link;
    revision_ = source.revision();
    known_ = std::move(nextKnown);
    return eve::Result<PixelChunkStreamUpdate>::success(std::move(update));
}

void PixelChunkStreamCursor::reset() noexcept {
    source_ = {};
    revision_ = 0;
    known_.clear();
}

}  // namespace eve::pixelworld_streaming
