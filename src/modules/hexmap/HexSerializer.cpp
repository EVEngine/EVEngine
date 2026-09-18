#include "hexmap/HexSerializer.h"

#include "common/Diagnostic.h"
#include "hexmap/HexMetrics.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace eve::hexmap {
namespace {

/** @brief Bytes of `kHexSaveMagic`. */
constexpr std::size_t kMagicSize = sizeof(kHexSaveMagic);

/** @brief Bytes before the per-cell records: magic, version, two dimensions, seed and revision. */
constexpr std::size_t kHeaderSize = kMagicSize + 4 + 4 + 4 + 4 + 8;

/** @brief Bytes per cell record: the packed values word plus the packed flags word. */
constexpr std::size_t kCellRecordSize = 8;

/** @brief Bytes per unit record: a cell index plus a facing angle. */
constexpr std::size_t kUnitRecordSize = 8;

/**
 * @brief Largest accepted grid dimension.
 *
 * A stored payload is untrusted input, so the dimensions are bounded before any
 * storage is sized from them. 512 keeps one grid at most 512 x 512 cells, which
 * matches the smallest sane bound for an interactive editor grid.
 */
constexpr std::uint32_t kMaxGridDimension = 512;

/** @brief Diagnostic for a payload that cannot be decoded. */
[[nodiscard]] Diagnostic invalidArgument(std::string message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, std::move(message), "hexmap.save");
}

/** @brief Appends a little-endian 32-bit word. */
void pushU32(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

/** @brief Appends a little-endian 64-bit word. */
void pushU64(std::vector<std::uint8_t>& out, std::uint64_t value) {
    for (std::uint32_t shift = 0; shift < 64; shift += 8)
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
}

/** @brief Appends a little-endian signed 32-bit word. */
void pushI32(std::vector<std::uint8_t>& out, std::int32_t value) { pushU32(out, static_cast<std::uint32_t>(value)); }

/** @brief Appends the little-endian bit pattern of a float. */
void pushF32(std::vector<std::uint8_t>& out, float value) { pushU32(out, std::bit_cast<std::uint32_t>(value)); }

/**
 * @brief Bounds-checked little-endian cursor over a payload.
 *
 * A short read leaves the cursor where it is and clears `ok`; every accessor then
 * returns zero, so a caller that checks `ok` once after a run of reads cannot read
 * past the buffer. Input bytes are never reinterpreted as a struct.
 */
class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) noexcept : data_(data), size_(size) {}

    /** @brief Whether every read so far fitted inside the payload. */
    [[nodiscard]] bool ok() const noexcept { return ok_; }
    /** @brief Current absolute cursor position. */
    [[nodiscard]] std::size_t offset() const noexcept { return offset_; }
    /** @brief Bytes left after the cursor. */
    [[nodiscard]] std::size_t remaining() const noexcept { return size_ - offset_; }

    /** @brief Absolute seek. @return False when `offset` is past the end. */
    [[nodiscard]] bool seek(std::size_t offset) noexcept {
        if (offset > size_) {
            ok_ = false;
            return false;
        }
        offset_ = offset;
        return true;
    }

    /** @brief Relative skip. @return False when the payload ends first. */
    [[nodiscard]] bool skip(std::size_t count) noexcept { return seek(offset_ + count); }

    [[nodiscard]] std::uint32_t u32() noexcept {
        if (remaining() < 4) {
            ok_ = false;
            return 0u;
        }
        std::uint32_t value = 0;
        for (std::size_t index = 0; index < 4; ++index)
            value |= static_cast<std::uint32_t>(data_[offset_ + index]) << (8u * static_cast<unsigned>(index));
        offset_ += 4;
        return value;
    }

    [[nodiscard]] std::uint64_t u64() noexcept {
        if (remaining() < 8) {
            ok_ = false;
            return 0u;
        }
        std::uint64_t value = 0;
        for (std::size_t index = 0; index < 8; ++index)
            value |= static_cast<std::uint64_t>(data_[offset_ + index]) << (8u * static_cast<unsigned>(index));
        offset_ += 8;
        return value;
    }

    [[nodiscard]] std::int32_t i32() noexcept { return static_cast<std::int32_t>(u32()); }

    [[nodiscard]] float f32() noexcept { return std::bit_cast<float>(u32()); }

private:
    const std::uint8_t* data_   = nullptr;
    std::size_t         size_   = 0;
    std::size_t         offset_ = 0;
    bool                ok_     = true;
};

}  // namespace

// --- writing -----------------------------------------------------------------

Result<void> saveHexMap(const HexMap& map, const std::vector<HexUnitState>& units, std::vector<std::uint8_t>& out) {
    if (map.empty()) return Result<void>::failure(invalidArgument("cannot serialize an empty hex map"));

    const std::int32_t cellCount = map.cellCount();
    const std::size_t  payload =
        kHeaderSize + static_cast<std::size_t>(cellCount) * kCellRecordSize + 4 + units.size() * kUnitRecordSize;
    out.clear();
    out.reserve(payload);

    for (const std::uint8_t byte : kHexSaveMagic) out.push_back(byte);
    pushU32(out, kHexSaveVersion);
    pushU32(out, static_cast<std::uint32_t>(map.cellCountX()));
    pushU32(out, static_cast<std::uint32_t>(map.cellCountZ()));
    pushU32(out, map.seed());
    pushU64(out, map.revision());

    for (std::int32_t index = 0; index < cellCount; ++index) {
        // `cellAt` is non-null for every index below `cellCount()`, the loop bound.
        const HexCellData* cell = map.cellAt(index);
        pushU32(out, cell->values.raw());
        pushU32(out, cell->flags.raw());
    }

    pushU32(out, static_cast<std::uint32_t>(units.size()));
    for (const HexUnitState& unit : units) {
        pushI32(out, unit.locationIndex);
        pushF32(out, unit.orientation);
    }

    return Result<void>::success();
}

// --- reading -----------------------------------------------------------------

Result<void> loadHexMap(const std::vector<std::uint8_t>& bytes, HexMap& map, std::vector<HexUnitState>& units) {
    if (bytes.size() < kHeaderSize) return Result<void>::failure(invalidArgument("hex save payload is truncated"));

    for (std::size_t index = 0; index < kMagicSize; ++index) {
        if (bytes[index] != kHexSaveMagic[index])
            return Result<void>::failure(invalidArgument("hex save payload has an unknown magic prefix"));
    }

    Reader reader(bytes.data(), bytes.size());
    // The payload size check above already covers the magic, so this can only fail
    // if that check is ever relaxed.
    if (!reader.skip(kMagicSize)) return Result<void>::failure(invalidArgument("hex save payload is truncated"));

    const std::uint32_t version = reader.u32();
    if (version != kHexSaveVersion) {
        return Result<void>::failure(invalidArgument("hex save version " + std::to_string(version) +
                                                     " is not supported (expected " + std::to_string(kHexSaveVersion) +
                                                     ")"));
    }
    const std::uint32_t cellCountX = reader.u32();
    const std::uint32_t cellCountZ = reader.u32();
    const std::uint32_t seed       = reader.u32();
    // Diagnostic only: the restored map's revision is whatever `reset` produces.
    const std::uint64_t savedRevision = reader.u64();
    (void)savedRevision;

    if (cellCountX == 0 || cellCountZ == 0)
        return Result<void>::failure(invalidArgument("hex save grid dimensions must be positive"));
    if (cellCountX > kMaxGridDimension || cellCountZ > kMaxGridDimension)
        return Result<void>::failure(invalidArgument("hex save grid dimensions exceed the 512 cell limit"));
    if (cellCountX % static_cast<std::uint32_t>(HexMetrics::kChunkSizeX) != 0u ||
        cellCountZ % static_cast<std::uint32_t>(HexMetrics::kChunkSizeZ) != 0u)
        return Result<void>::failure(
            invalidArgument("hex save grid dimensions must be a multiple of the 5x5 chunk size"));

    const std::size_t cellCount  = static_cast<std::size_t>(cellCountX) * static_cast<std::size_t>(cellCountZ);
    const std::size_t cellsEnd   = kHeaderSize + cellCount * kCellRecordSize;
    const std::size_t unitsStart = cellsEnd + 4;
    if (bytes.size() < unitsStart)
        return Result<void>::failure(invalidArgument("hex save payload is truncated before the unit records"));

    if (!reader.seek(cellsEnd)) return Result<void>::failure(invalidArgument("hex save payload is truncated"));
    const std::uint32_t unitCount = reader.u32();
    if (static_cast<std::size_t>(unitCount) > (bytes.size() - unitsStart) / kUnitRecordSize)
        return Result<void>::failure(invalidArgument("hex save unit count exceeds the payload size"));
    if (unitsStart + static_cast<std::size_t>(unitCount) * kUnitRecordSize != bytes.size())
        return Result<void>::failure(invalidArgument("hex save payload size does not match its contents"));

    // Decode into locals first: a rejected payload must leave both outputs alone.
    std::vector<HexCellData> cells(cellCount);
    if (!reader.seek(kHeaderSize)) return Result<void>::failure(invalidArgument("hex save payload is truncated"));
    for (std::size_t index = 0; index < cellCount; ++index) {
        cells[index].values = HexValues{reader.u32()};
        cells[index].flags  = HexFlags{reader.u32()};
    }

    std::vector<HexUnitState> restored(unitCount);
    if (!reader.seek(unitsStart)) return Result<void>::failure(invalidArgument("hex save payload is truncated"));
    for (std::uint32_t index = 0; index < unitCount; ++index) {
        const std::int32_t locationIndex = reader.i32();
        const float        orientation   = reader.f32();
        if (locationIndex < 0 || static_cast<std::size_t>(locationIndex) >= cellCount)
            return Result<void>::failure(invalidArgument("hex save unit cell is outside the grid"));
        restored[index].locationIndex = locationIndex;
        restored[index].orientation   = orientation;
    }
    if (!reader.ok()) return Result<void>::failure(invalidArgument("hex save payload is truncated"));

    // The unit list must be legal *before* the grid is adopted, because the unit
    // registry consumes it afterwards and would otherwise reject the whole list
    // against a grid the caller had already replaced. `canHoldUnit` is the same
    // terrain rule the registry applies through `isValidDestination`.
    for (std::size_t index = 0; index < restored.size(); ++index) {
        const auto cell = static_cast<std::size_t>(restored[index].locationIndex);
        if (!canHoldUnit(cells[cell]))
            return Result<void>::failure(invalidArgument("hex save unit stands on an unusable cell"));
        for (std::size_t other = 0; other < index; ++other) {
            if (restored[other].locationIndex == restored[index].locationIndex)
                return Result<void>::failure(invalidArgument("hex save units occupy the same cell"));
        }
    }

    // Everything decoded; only now are the outputs rebuilt.
    auto resized = map.reset(static_cast<std::int32_t>(cellCountX), static_cast<std::int32_t>(cellCountZ), seed);
    if (!resized.ok()) return Result<void>::failure(resized.status());
    for (std::size_t index = 0; index < cellCount; ++index) {
        map.setCellState(map.coordinatesAt(static_cast<std::int32_t>(index)), cells[index].values, cells[index].flags)
            .ignore("every index below the freshly reset cell count is inside the grid");
    }
    map.markAllChunksDirty();
    units = std::move(restored);
    return Result<void>::success();
}

}  // namespace eve::hexmap
