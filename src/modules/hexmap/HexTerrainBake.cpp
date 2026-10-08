/**
 * @file HexTerrainBake.cpp
 * @brief Encode, decode and apply a hex terrain bake without generating one.
 */

#include "hexmap/HexTerrainBake.h"

#include "common/Diagnostic.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace eve::hexmap {
namespace {

[[nodiscard]] 
[[nodiscard]] Result<HexTerrainBake> invalidBake(std::string message) {
    return Result<HexTerrainBake>::failure(
        Diagnostic::error(DiagnosticCode::InvalidArgument, message, "hexmap"));
}

void writeU32LE(unsigned char* bytes, std::uint32_t value) noexcept {
    bytes[0] = static_cast<unsigned char>(value & 0xffu);
    bytes[1] = static_cast<unsigned char>((value >> 8) & 0xffu);
    bytes[2] = static_cast<unsigned char>((value >> 16) & 0xffu);
    bytes[3] = static_cast<unsigned char>((value >> 24) & 0xffu);
}

[[nodiscard]] std::uint32_t readU32LE(const unsigned char* bytes) noexcept {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}

[[nodiscard]] std::string packCells(const std::vector<HexCellData>& cells) {
    std::string packed(cells.size() * 8u, '\0');
    auto*       bytes = reinterpret_cast<unsigned char*>(packed.data());
    for (std::size_t i = 0; i < cells.size(); ++i) {
        writeU32LE(bytes + i * 8u, cells[i].values.raw());
        writeU32LE(bytes + i * 8u + 4u, cells[i].flags.raw());
    }
    return packed;
}

[[nodiscard]] Result<std::vector<HexCellData>> unpackCells(std::string_view packed, std::size_t expected) {
    if (packed.size() != expected * 8u)
        return Result<std::vector<HexCellData>>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              "hex terrain bake cell payload length does not match the cell count", "hexmap"));
    std::vector<HexCellData> cells(expected);
    const auto*              bytes = reinterpret_cast<const unsigned char*>(packed.data());
    for (std::size_t i = 0; i < expected; ++i) {
        cells[i].values = HexValues{readU32LE(bytes + i * 8u)};
        cells[i].flags  = HexFlags{readU32LE(bytes + i * 8u + 4u)};
    }
    return Result<std::vector<HexCellData>>::success(std::move(cells));
}

[[nodiscard]] Result<std::int32_t> readInt32(const Value& object, const char* key) {
    const Value* field = object.find(key);
    if (field == nullptr || !field->isNumeric())
        return Result<std::int32_t>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              std::string("hex terrain bake is missing numeric '") + key + "'", "hexmap"));
    if (field->isInt64()) return Result<std::int32_t>::success(static_cast<std::int32_t>(field->asInt()));
    return Result<std::int32_t>::success(static_cast<std::int32_t>(field->asDouble()));
}

[[nodiscard]] Result<std::uint32_t> readUInt32(const Value& object, const char* key) {
    auto parsed = readInt32(object, key);
    if (!parsed) return Result<std::uint32_t>::failure(parsed.status());
    if (parsed.value() < 0)
        return Result<std::uint32_t>::failure(
            Diagnostic::error(DiagnosticCode::InvalidArgument,
                              std::string("hex terrain bake '") + key + "' must not be negative", "hexmap"));
    return Result<std::uint32_t>::success(static_cast<std::uint32_t>(parsed.value()));
}

}  // namespace

HexTerrainBake snapshotHexTerrain(const HexMap& map) {
    HexTerrainBake bake;
    bake.kind       = HexTerrainBake::Kind::Planar;
    bake.cellCountX = map.cellCountX();
    bake.cellCountZ = map.cellCountZ();
    bake.cellCount  = map.cellCount();
    bake.seed       = map.seed();
    bake.cells.resize(static_cast<std::size_t>(bake.cellCount));
    for (std::int32_t index = 0; index < bake.cellCount; ++index) {
        const HexCellData* cell = map.cellAt(index);
        if (cell != nullptr) bake.cells[static_cast<std::size_t>(index)] = *cell;
    }
    return bake;
}

HexTerrainBake snapshotHexSphereTerrain(const HexSphereMap& map) {
    HexTerrainBake bake;
    bake.kind        = HexTerrainBake::Kind::Sphere;
    bake.cellCount   = map.cellCount();
    bake.subdivision = map.subdivision();
    bake.radius      = map.sphereRadius();
    bake.seed        = map.seed();
    bake.cells.resize(static_cast<std::size_t>(bake.cellCount));
    for (std::int32_t cell = 0; cell < bake.cellCount; ++cell) {
        const HexCellData* record = map.cellAt(cell);
        if (record != nullptr) bake.cells[static_cast<std::size_t>(cell)] = *record;
    }
    return bake;
}

Value hexTerrainBakeToValue(const HexTerrainBake& bake) {
    Value payload = Value::object({});
    if (bake.kind == HexTerrainBake::Kind::Sphere) {
        payload.set("kind", Value("hex.sphere"));
        payload.set("subdivision", Value(static_cast<std::int64_t>(bake.subdivision)));
        payload.set("radius", Value(static_cast<double>(bake.radius)));
        payload.set("cellCount", Value(static_cast<std::int64_t>(bake.cellCount)));
    } else {
        payload.set("kind", Value("hex.terrain"));
        payload.set("cellCountX", Value(static_cast<std::int64_t>(bake.cellCountX)));
        payload.set("cellCountZ", Value(static_cast<std::int64_t>(bake.cellCountZ)));
    }
    payload.set("seed", Value(static_cast<std::int64_t>(bake.seed)));
    payload.set("cells", Value(packCells(bake.cells)));
    return payload;
}

Result<HexTerrainBake> hexTerrainBakeFromValue(const Value& value) {
    if (!value.isObject()) return invalidBake("hex terrain bake must be an object");
    const Value* kindField = value.find("kind");
    if (kindField == nullptr || !kindField->isString()) return invalidBake("hex terrain bake is missing string 'kind'");
    const std::string& kind = kindField->asString();

    HexTerrainBake bake;
    if (kind == "hex.sphere") {
        bake.kind        = HexTerrainBake::Kind::Sphere;
        auto subdivision = readInt32(value, "subdivision");
        if (!subdivision) return Result<HexTerrainBake>::failure(subdivision.status());
        bake.subdivision         = subdivision.value();
        const Value* radiusField = value.find("radius");
        if (radiusField == nullptr || !radiusField->isNumeric())
            return invalidBake("hex sphere bake is missing numeric 'radius'");
        bake.radius    = radiusField->isDouble() ? static_cast<float>(radiusField->asDouble())
                                                 : static_cast<float>(radiusField->asInt());
        auto cellCount = readInt32(value, "cellCount");
        if (!cellCount) return Result<HexTerrainBake>::failure(cellCount.status());
        bake.cellCount = cellCount.value();
    } else if (kind == "hex.terrain") {
        bake.kind       = HexTerrainBake::Kind::Planar;
        auto cellCountX = readInt32(value, "cellCountX");
        auto cellCountZ = readInt32(value, "cellCountZ");
        if (!cellCountX) return Result<HexTerrainBake>::failure(cellCountX.status());
        if (!cellCountZ) return Result<HexTerrainBake>::failure(cellCountZ.status());
        bake.cellCountX = cellCountX.value();
        bake.cellCountZ = cellCountZ.value();
        bake.cellCount  = bake.cellCountX * bake.cellCountZ;
    } else {
        return invalidBake("hex terrain bake kind must be 'hex.terrain' or 'hex.sphere'");
    }

    auto seed = readUInt32(value, "seed");
    if (!seed) return Result<HexTerrainBake>::failure(seed.status());
    bake.seed = seed.value();

    const Value* cellsField = value.find("cells");
    if (cellsField == nullptr || !cellsField->isString())
        return invalidBake("hex terrain bake is missing packed 'cells'");
    if (bake.cellCount < 0) return invalidBake("hex terrain bake cell count must not be negative");
    auto cells = unpackCells(cellsField->asString(), static_cast<std::size_t>(bake.cellCount));
    if (!cells) return Result<HexTerrainBake>::failure(cells.status());
    bake.cells = std::move(cells).takeValue();
    return Result<HexTerrainBake>::success(std::move(bake));
}

Result<void> applyHexTerrain(HexMap& map, const HexTerrainBake& bake) {
    if (bake.kind != HexTerrainBake::Kind::Planar) return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "applyHexTerrain requires a planar bake", "hexmap"));
    if (bake.cellCountX <= 0 || bake.cellCountZ <= 0) return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "planar bake size must be positive", "hexmap"));
    if (static_cast<std::int32_t>(bake.cells.size()) != bake.cellCountX * bake.cellCountZ)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "planar bake cell count does not match width and height", "hexmap"));

    auto reset = map.reset(bake.cellCountX, bake.cellCountZ, bake.seed);
    if (!reset) return reset;
    for (std::int32_t index = 0; index < map.cellCount(); ++index) {
        const HexCellData& cell    = bake.cells[static_cast<std::size_t>(index)];
        auto               written = map.setCellState(map.coordinatesAt(index), cell.values, cell.flags);
        if (!written) return written;
    }
    map.markAllChunksDirty();
    return Result<void>::success();
}

Result<void> applyHexSphereTerrain(HexSphereMap& map, const HexTerrainBake& bake) {
    if (bake.kind != HexTerrainBake::Kind::Sphere)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "applyHexSphereTerrain requires a sphere bake", "hexmap"));
    if (map.empty()) return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "applyHexSphereTerrain requires an existing sphere topology", "hexmap"));
    if (bake.cellCount <= 0 || static_cast<std::int32_t>(bake.cells.size()) != bake.cellCount)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "sphere bake cell count is inconsistent", "hexmap"));
    if (map.cellCount() != bake.cellCount || map.subdivision() != bake.subdivision)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument, "sphere bake does not match the live topology; call newSphere with the same subdivision", "hexmap"));

    for (std::int32_t cell = 0; cell < bake.cellCount; ++cell) {
        const HexCellData& record  = bake.cells[static_cast<std::size_t>(cell)];
        auto               written = map.setCellState(cell, record.values, record.flags);
        if (!written) return written;
    }
    map.markAllDirty();
    return Result<void>::success();
}

}  // namespace eve::hexmap
