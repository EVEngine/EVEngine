#include "procgen/road/RoadTerrainWorkspaceInternal.h"

#include <cmath>
#include <limits>

namespace eve::procgen::road {
namespace {
template <class T>
Result<T> invalidSnapshot() {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                "invalid procgen.road.terrain version 1/2 snapshot", "snapshot"));
}
Value encodeRaster(const Heightmap& map) {
    Value::Array samples;
    for (float v : map.data()) samples.emplace_back(v);
    return Value::Object{{"width", map.getWidth()}, {"height", map.getHeight()}, {"samples", std::move(samples)}};
}
Value encodeTile(const RoadTerrainTile& tile) {
    Value::Array splat, details, trees;
    for (const auto& map : tile.splatLayers) splat.push_back(encodeRaster(map));
    for (const auto& detail : tile.details) {
        Value::Array samples;
        for (int v : detail.density) samples.emplace_back(v);
        details.emplace_back(Value::Object{{"resourceId", detail.resourceId},
                                           {"width", detail.width},
                                           {"height", detail.height},
                                           {"samples", std::move(samples)}});
    }
    for (const auto& tree : tile.trees)
        trees.emplace_back(Value::Object{{"id", tree.id},
                                         {"resourceId", tree.resourceId},
                                         {"x", tree.x},
                                         {"y", tree.y},
                                         {"z", tree.z},
                                         {"yaw", tree.yaw},
                                         {"width", tree.width},
                                         {"height", tree.height}});
    return Value::Object{{"id", tile.id},
                         {"originX", tile.originX},
                         {"originY", tile.originY},
                         {"originZ", tile.originZ},
                         {"sizeX", tile.sizeX},
                         {"sizeZ", tile.sizeZ},
                         {"heightScale", tile.heightScale},
                         {"heights", encodeRaster(tile.heights)},
                         {"holes", encodeRaster(tile.holes)},
                         {"splatLayers", std::move(splat)},
                         {"details", std::move(details)},
                         {"trees", std::move(trees)}};
}
bool number(const Value* value, float& out) {
    if (!value || (!value->isDouble() && !value->isInt64())) return false;
    const double n = value->isDouble() ? value->asDouble() : static_cast<double>(value->asInt());
    if (!std::isfinite(n) || std::fabs(n) > std::numeric_limits<float>::max()) return false;
    out = static_cast<float>(n);
    return true;
}
bool integer(const Value* value, int& out, int maximum = 4096) {
    if (!value || !value->isInt64() || value->asInt() < 0 || value->asInt() > maximum) return false;
    out = static_cast<int>(value->asInt());
    return true;
}
bool text(const Value* value, std::string& out) {
    if (!value || !value->isString() || value->asString().empty() || value->asString().size() > 256u) return false;
    out = value->asString();
    return true;
}
const Value::Array* array(const Value* v) { return v ? v->getIf<Value::Array>() : nullptr; }
bool                dimensions(const Value& value, int& width, int& height, std::size_t& budget) {
    if (!integer(value.find("width"), width) || !integer(value.find("height"), height) || width < 1 || height < 1)
        return false;
    const auto count = static_cast<std::size_t>(width) * height;
    if (count > budget) return false;
    budget -= count;
    return true;
}
bool raster(const Value* value, Heightmap& out, std::size_t& budget) {
    int width = 0, height = 0;
    if (!value || !dimensions(*value, width, height, budget)) return false;
    const auto* samples = array(value->find("samples"));
    if (!samples || samples->size() != static_cast<std::size_t>(width) * height) return false;
    out = Heightmap(width, height);
    for (std::size_t i = 0; i < samples->size(); ++i)
        if (!number(&(*samples)[i], out.data()[i])) return false;
    return true;
}
bool decodeTile(const Value& value, RoadTerrainTile& tile, std::size_t& budget, bool requireOriginY) {
    if ((requireOriginY || value.find("originY")) && !number(value.find("originY"), tile.originY)) return false;
    if (!text(value.find("id"), tile.id) || !number(value.find("originX"), tile.originX) ||
        !number(value.find("originZ"), tile.originZ) || !number(value.find("sizeX"), tile.sizeX) ||
        !number(value.find("sizeZ"), tile.sizeZ) || !number(value.find("heightScale"), tile.heightScale) ||
        !raster(value.find("heights"), tile.heights, budget) || !raster(value.find("holes"), tile.holes, budget))
        return false;
    const auto *splat = array(value.find("splatLayers")), *details = array(value.find("details")),
               *trees = array(value.find("trees"));
    if (!splat || splat->size() > 64u || !details || details->size() > 256u || !trees || trees->size() > 1000000u ||
        trees->size() > budget / 8u)
        return false;
    budget -= trees->size() * 8u;
    for (const auto& encoded : *splat) {
        Heightmap map;
        if (!raster(&encoded, map, budget)) return false;
        tile.splatLayers.push_back(std::move(map));
    }
    for (const auto& encoded : *details) {
        RoadTerrainDetail d;
        if (!text(encoded.find("resourceId"), d.resourceId) || !dimensions(encoded, d.width, d.height, budget))
            return false;
        const auto* samples = array(encoded.find("samples"));
        if (!samples || samples->size() != static_cast<std::size_t>(d.width) * d.height) return false;
        for (const auto& sample : *samples) {
            int density = 0;
            if (!integer(&sample, density, std::numeric_limits<int>::max())) return false;
            d.density.push_back(density);
        }
        tile.details.push_back(std::move(d));
    }
    for (const auto& encoded : *trees) {
        RoadTerrainTree t;
        if (!text(encoded.find("id"), t.id) || !text(encoded.find("resourceId"), t.resourceId) ||
            !number(encoded.find("x"), t.x) || !number(encoded.find("y"), t.y) || !number(encoded.find("z"), t.z) ||
            !number(encoded.find("yaw"), t.yaw) || !number(encoded.find("width"), t.width) ||
            !number(encoded.find("height"), t.height))
            return false;
        tile.trees.push_back(std::move(t));
    }
    return true;
}
bool sameTopology(const RoadTerrainTile& a, const RoadTerrainTile& b) {
    if (a.id != b.id || a.originX != b.originX || a.originY != b.originY || a.originZ != b.originZ ||
        a.sizeX != b.sizeX || a.sizeZ != b.sizeZ || a.heightScale != b.heightScale ||
        a.heights.getWidth() != b.heights.getWidth() || a.heights.getHeight() != b.heights.getHeight() ||
        a.holes.getWidth() != b.holes.getWidth() || a.holes.getHeight() != b.holes.getHeight() ||
        a.splatLayers.size() != b.splatLayers.size() || a.details.size() != b.details.size())
        return false;
    for (std::size_t i = 0; i < a.splatLayers.size(); ++i)
        if (a.splatLayers[i].getWidth() != b.splatLayers[i].getWidth() ||
            a.splatLayers[i].getHeight() != b.splatLayers[i].getHeight())
            return false;
    for (std::size_t i = 0; i < a.details.size(); ++i)
        if (a.details[i].resourceId != b.details[i].resourceId || a.details[i].width != b.details[i].width ||
            a.details[i].height != b.details[i].height)
            return false;
    return true;
}
}  // namespace
Result<Value> RoadTerrainWorkspace::capture() const {
    if (!impl_ || impl_->history.empty()) return invalidSnapshot<Value>();
    Value::Array history;
    for (const auto& frame : impl_->history) {
        Value::Array tiles;
        for (const auto& tile : frame) tiles.push_back(encodeTile(tile));
        history.emplace_back(std::move(tiles));
    }
    return Result<Value>::success(Value::Object{{"schema", "procgen.road.terrain"},
                                                {"version", 2},
                                                {"cursor", static_cast<std::int64_t>(impl_->cursor)},
                                                {"history", std::move(history)}});
}
Result<int> RoadTerrainWorkspace::restore(const Value& snapshot) {
    const auto *schema = snapshot.find("schema"), *version = snapshot.find("version");
    const auto* history = array(snapshot.find("history"));
    int         cursor  = 0;
    if (!schema || !schema->isString() || schema->asString() != "procgen.road.terrain" || !version ||
        !version->isInt64() || (version->asInt() != 1 && version->asInt() != 2) || !history || history->empty() ||
        history->size() > 4096u || !integer(snapshot.find("cursor"), cursor) ||
        static_cast<std::size_t>(cursor) >= history->size())
        return invalidSnapshot<int>();
    auto        candidate = std::make_unique<Impl>();
    std::size_t budget    = 16u * 1024u * 1024u;
    for (const auto& encodedFrame : *history) {
        const auto* tiles = array(&encodedFrame);
        if (!tiles || tiles->empty() || tiles->size() > 256u) return invalidSnapshot<int>();
        Impl::Frame frame;
        for (const auto& encoded : *tiles) {
            RoadTerrainTile tile;
            if (!decodeTile(encoded, tile, budget, version->asInt() == 2)) return invalidSnapshot<int>();
            frame.push_back(std::move(tile));
        }
        auto valid = detail::validateTerrainTiles(frame);
        if (!valid.ok()) return Result<int>::failure(valid.status());
        if (!candidate->history.empty()) {
            const auto& baseline = candidate->history.front();
            if (frame.size() != baseline.size()) return invalidSnapshot<int>();
            for (std::size_t i = 0; i < frame.size(); ++i)
                if (!sameTopology(frame[i], baseline[i])) return invalidSnapshot<int>();
        }
        candidate->history.push_back(std::move(frame));
    }
    candidate->cursor = static_cast<std::size_t>(cursor);
    impl_.swap(candidate);
    return Result<int>::success(static_cast<int>(impl_->history[impl_->cursor].size()));
}
}  // namespace eve::procgen::road
