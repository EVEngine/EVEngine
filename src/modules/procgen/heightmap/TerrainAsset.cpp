#include "procgen/heightmap/TerrainAsset.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace eve::procgen {
namespace {
constexpr uint16_t kVersion = 5;
constexpr size_t kHeaderSize = 44;
constexpr size_t kEntrySize = 36;
constexpr size_t kBytesPerCell = 12;
size_t bytesPerCell(uint16_t version) {
    return version == 1 ? 7u : (version == 2 ? 8u :
           (version == 3 ? 9u : (version == 4 ? 11u : 12u)));
}

void fail(std::string *error, const char *message) { if (error) *error = message; }
void putU16(std::vector<uint8_t> &out, uint16_t v) { out.push_back(uint8_t(v)); out.push_back(uint8_t(v >> 8)); }
void putU32(std::vector<uint8_t> &out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(uint8_t(v >> (i * 8)));
}
void putI32(std::vector<uint8_t> &out, int32_t v) { putU32(out, uint32_t(v)); }
void putU64(std::vector<uint8_t> &out, uint64_t v) {
    for (int i = 0; i < 8; ++i) out.push_back(uint8_t(v >> (i * 8)));
}
void putFloat(std::vector<uint8_t> &out, float v) { uint32_t bits; std::memcpy(&bits, &v, 4); putU32(out, bits); }

bool getU16(const uint8_t *&p, const uint8_t *end, uint16_t &v) {
    if (end - p < 2) return false;
    v = uint16_t(p[0]) | uint16_t(p[1] << 8); p += 2; return true;
}
bool getU32(const uint8_t *&p, const uint8_t *end, uint32_t &v) {
    if (end - p < 4) return false;
    v = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; p += 4; return true;
}
bool getI32(const uint8_t *&p, const uint8_t *end, int32_t &v) { uint32_t u; if (!getU32(p, end, u)) return false; v = int32_t(u); return true; }
bool getU64(const uint8_t *&p, const uint8_t *end, uint64_t &v) {
    if (end - p < 8) return false;
    v = 0; for (int i = 0; i < 8; ++i) v |= uint64_t(p[i]) << (i * 8); p += 8; return true;
}
bool getFloat(const uint8_t *&p, const uint8_t *end, float &v) { uint32_t bits; if (!getU32(p, end, bits)) return false; std::memcpy(&v, &bits, 4); return true; }

uint32_t checksum(const std::vector<uint8_t> &data) {
    uint32_t value = 2166136261u;
    for (uint8_t byte : data) { value ^= byte; value *= 16777619u; }
    return value;
}

std::vector<uint8_t> packBits(const std::vector<uint8_t> &src) {
    std::vector<uint8_t> out; out.reserve(src.size()); size_t i = 0;
    while (i < src.size()) {
        size_t run = 1;
        while (i + run < src.size() && src[i + run] == src[i] && run < 128) ++run;
        if (run >= 3) { out.push_back(uint8_t(0x80u | uint8_t(run - 1))); out.push_back(src[i]); i += run; continue; }
        const size_t begin = i; i += run;
        while (i < src.size() && i - begin < 128) {
            size_t nextRun = 1;
            while (i + nextRun < src.size() && src[i + nextRun] == src[i] && nextRun < 3) ++nextRun;
            if (nextRun >= 3) break;
            i += std::min(nextRun, size_t(128) - (i - begin));
        }
        out.push_back(uint8_t(i - begin - 1)); out.insert(out.end(), src.begin() + ptrdiff_t(begin), src.begin() + ptrdiff_t(i));
    }
    return out;
}

bool unpackBits(const uint8_t *data, size_t size, size_t rawSize, std::vector<uint8_t> &out) {
    out.clear(); out.reserve(rawSize); size_t i = 0;
    while (i < size && out.size() < rawSize) {
        const uint8_t token = data[i++]; const size_t count = size_t(token & 0x7fu) + 1;
        if (count > rawSize - out.size()) return false;
        if (token & 0x80u) { if (i >= size) return false; out.insert(out.end(), count, data[i++]); }
        else { if (count > size - i) return false; out.insert(out.end(), data + i, data + i + count); i += count; }
    }
    return i == size && out.size() == rawSize;
}

uint8_t quantize8(float v) { return uint8_t(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); }
uint16_t quantize16(float v) { return uint16_t(std::lround(std::clamp(v, 0.f, 1.f) * 65535.f)); }
}  // namespace

bool TerrainAsset::bake(const Heightmap &hm, const HydrologyMap &hydro, const ClimateMap &climate,
                        int chunkSize, std::vector<uint8_t> &out, std::string *error) {
    const int w = hm.getWidth(), h = hm.getHeight(); const size_t cells = size_t(w) * size_t(h);
    if (w <= 0 || h <= 0 || chunkSize <= 0 || chunkSize > 65535) { fail(error, "terrain asset: invalid dimensions"); return false; }
    if (hydro.width != w || hydro.height != h || climate.width != w || climate.height != h ||
        hydro.flowAccumulation.size() != cells || hydro.rivers.size() != cells ||
        (!hydro.flowDirection.empty() && hydro.flowDirection.size() != cells) ||
        (!hydro.flowVectorX.empty() && hydro.flowVectorX.size() != cells) ||
        (!hydro.flowVectorY.empty() && hydro.flowVectorY.size() != cells) ||
        (hydro.flowVectorX.empty() != hydro.flowVectorY.empty()) ||
        (!hydro.streamOrder.empty() && hydro.streamOrder.size() != cells) ||
        (!hydro.lakeDepth.empty() && hydro.lakeDepth.size() != cells) ||
        climate.temperature.size() != cells || climate.moisture.size() != cells || climate.biomes.size() != cells) {
        fail(error, "terrain asset: layer dimensions do not match heightmap"); return false;
    }
    const auto [minIt, maxIt] = std::minmax_element(hm.data().begin(), hm.data().end());
    const float minHeight = *minIt, maxHeight = *maxIt;
    const float maxFlow = std::max(1.f, *std::max_element(hydro.flowAccumulation.begin(), hydro.flowAccumulation.end()));
    struct Block { TerrainChunkEntry entry; std::vector<uint8_t> bytes; };
    std::vector<Block> blocks;
    for (int cy = 0; cy * chunkSize < h; ++cy) for (int cx = 0; cx * chunkSize < w; ++cx) {
        Block block; block.entry.chunkX = cx; block.entry.chunkY = cy;
        block.entry.width = std::min(chunkSize, w - cx * chunkSize);
        block.entry.height = std::min(chunkSize, h - cy * chunkSize);
        std::vector<uint8_t> raw; raw.reserve(size_t(block.entry.width) * size_t(block.entry.height) * kBytesPerCell);
        for (int ly = 0; ly < block.entry.height; ++ly) for (int lx = 0; lx < block.entry.width; ++lx) {
            const size_t i = size_t(cy * chunkSize + ly) * size_t(w) + size_t(cx * chunkSize + lx);
            const float normalizedHeight = maxHeight > minHeight ? (hm.data()[i] - minHeight) / (maxHeight - minHeight) : 0.f;
            const uint16_t qh = quantize16(normalizedHeight); putU16(raw, qh);
            raw.push_back(quantize8(hydro.flowAccumulation[i] / maxFlow));
            const int direction = hydro.flowDirection.empty() ? -1 : int(hydro.flowDirection[i]);
            if (direction < -1 || direction > 7) {
                fail(error, "terrain asset: invalid flow direction"); return false;
            }
            raw.push_back(uint8_t(direction + 1));
            float flowX = 0.f, flowY = 0.f;
            if (!hydro.flowVectorX.empty()) {
                flowX = hydro.flowVectorX[i]; flowY = hydro.flowVectorY[i];
            } else if (direction >= 0) {
                static constexpr std::array<float, 8> vectorX{
                    -0.70710678f, 0.f, 0.70710678f, -1.f, 1.f,
                    -0.70710678f, 0.f, 0.70710678f};
                static constexpr std::array<float, 8> vectorY{
                    -0.70710678f, -1.f, -0.70710678f, 0.f, 0.f,
                    0.70710678f, 1.f, 0.70710678f};
                flowX = vectorX[size_t(direction)]; flowY = vectorY[size_t(direction)];
            }
            if (!std::isfinite(flowX) || !std::isfinite(flowY)) {
                fail(error, "terrain asset: invalid continuous flow vector"); return false;
            }
            raw.push_back(quantize8(flowX * 0.5f + 0.5f));
            raw.push_back(quantize8(flowY * 0.5f + 0.5f));
            raw.push_back(hydro.streamOrder.empty() ? 0 : hydro.streamOrder[i]);
            const float lakeDepth = hydro.lakeDepth.empty() ? 0.f : hydro.lakeDepth[i];
            raw.push_back(quantize8(maxHeight > minHeight ? lakeDepth / (maxHeight - minHeight) : 0.f));
            raw.push_back(quantize8(climate.temperature[i])); raw.push_back(quantize8(climate.moisture[i]));
            raw.push_back(hydro.rivers[i] ? 1 : 0); raw.push_back(uint8_t(climate.biomes[i]));
        }
        block.entry.rawSize = uint32_t(raw.size()); block.entry.checksum = checksum(raw);
        std::vector<uint8_t> packed = packBits(raw);
        block.entry.compressed = packed.size() < raw.size();
        block.bytes = block.entry.compressed ? std::move(packed) : std::move(raw);
        block.entry.storedSize = uint32_t(block.bytes.size()); blocks.push_back(std::move(block));
    }
    const uint64_t payloadStart = kHeaderSize + uint64_t(kEntrySize) * blocks.size(); uint64_t cursor = payloadStart;
    for (Block &block : blocks) { block.entry.offset = cursor; cursor += block.bytes.size(); }
    out.clear(); out.reserve(size_t(cursor)); out.insert(out.end(), {'E', 'V', 'T', 'R'});
    putU16(out, kVersion); putU16(out, 0); putU32(out, uint32_t(w)); putU32(out, uint32_t(h));
    putU32(out, uint32_t(chunkSize)); putU32(out, uint32_t(blocks.size()));
    putFloat(out, minHeight); putFloat(out, maxHeight); putFloat(out, maxFlow); putU64(out, kHeaderSize);
    for (const Block &block : blocks) {
        const auto &e = block.entry; putI32(out, e.chunkX); putI32(out, e.chunkY); putU16(out, uint16_t(e.width)); putU16(out, uint16_t(e.height));
        out.push_back(e.compressed ? 1 : 0); out.insert(out.end(), 3, 0); putU64(out, e.offset);
        putU32(out, e.storedSize); putU32(out, e.rawSize); putU32(out, e.checksum);
    }
    for (const Block &block : blocks) out.insert(out.end(), block.bytes.begin(), block.bytes.end());
    return true;
}

bool TerrainAsset::open(const uint8_t *data, size_t size, std::string *error) {
    if (!data || size < kHeaderSize || std::memcmp(data, "EVTR", 4) != 0) { fail(error, "terrain asset: invalid magic or truncated header"); return false; }
    const uint8_t *p = data + 4, *end = data + size; uint16_t version = 0, flags = 0; uint32_t w, h, cs, count; uint64_t directory;
    float minH, maxH, maxFlow;
    if (!getU16(p, end, version) || !getU16(p, end, flags) || !getU32(p, end, w) || !getU32(p, end, h) ||
        !getU32(p, end, cs) || !getU32(p, end, count) || !getFloat(p, end, minH) || !getFloat(p, end, maxH) ||
        !getFloat(p, end, maxFlow) || !getU64(p, end, directory) ||
        version < 1 || version > kVersion || flags != 0 ||
        w == 0 || h == 0 || cs == 0 || w > uint32_t(std::numeric_limits<int>::max()) ||
        h > uint32_t(std::numeric_limits<int>::max()) || cs > uint32_t(std::numeric_limits<int>::max()) ||
        !std::isfinite(minH) || !std::isfinite(maxH) || !std::isfinite(maxFlow) ||
        minH > maxH || maxFlow < 1.f || directory != kHeaderSize ||
        uint64_t(count) != ((uint64_t(w) + cs - 1) / cs) * ((uint64_t(h) + cs - 1) / cs) ||
        uint64_t(count) * kEntrySize > size - directory) {
        fail(error, "terrain asset: unsupported or invalid header"); return false;
    }
    const uint64_t payloadStart = directory + uint64_t(count) * kEntrySize;
    std::vector<TerrainChunkEntry> entries; entries.reserve(count); p = data + directory;
    for (uint32_t i = 0; i < count; ++i) {
        TerrainChunkEntry e; int32_t x, y; uint16_t cw, ch; uint8_t compression;
        if (!getI32(p, end, x) || !getI32(p, end, y) || !getU16(p, end, cw) || !getU16(p, end, ch) || end - p < 4) {
            fail(error, "terrain asset: truncated directory"); return false;
        }
        compression = p[0];
        const bool reservedClear = p[1] == 0 && p[2] == 0 && p[3] == 0;
        p += 4; e.chunkX = x; e.chunkY = y; e.width = cw; e.height = ch; e.compressed = compression == 1;
        const bool coordinateValid = x >= 0 && y >= 0 && uint64_t(x) * cs < w && uint64_t(y) * cs < h;
        const uint32_t expectedWidth = coordinateValid ? std::min(cs, w - uint32_t(x) * cs) : 0;
        const uint32_t expectedHeight = coordinateValid ? std::min(cs, h - uint32_t(y) * cs) : 0;
        if (compression > 1 || !getU64(p, end, e.offset) || !getU32(p, end, e.storedSize) || !getU32(p, end, e.rawSize) || !getU32(p, end, e.checksum) ||
            !reservedClear || !coordinateValid || uint32_t(e.width) != expectedWidth ||
            uint32_t(e.height) != expectedHeight ||
            e.rawSize != uint64_t(e.width) * uint64_t(e.height) * bytesPerCell(version) ||
            (!e.compressed && e.storedSize != e.rawSize) || e.offset < payloadStart ||
            e.offset > size || e.storedSize > size - e.offset) {
            fail(error, "terrain asset: invalid chunk entry"); return false;
        }
        if (std::any_of(entries.begin(), entries.end(), [&](const auto &old) { return old.chunkX == e.chunkX && old.chunkY == e.chunkY; })) {
            fail(error, "terrain asset: duplicate chunk coordinate"); return false;
        }
        entries.push_back(e);
    }
    std::vector<const TerrainChunkEntry *> intervals;
    intervals.reserve(entries.size());
    for (const auto &entry : entries) intervals.push_back(&entry);
    std::sort(intervals.begin(), intervals.end(), [](const auto *a, const auto *b) {
        return a->offset < b->offset;
    });
    for (size_t i = 1; i < intervals.size(); ++i) {
        if (intervals[i - 1]->offset + intervals[i - 1]->storedSize > intervals[i]->offset) {
            fail(error, "terrain asset: overlapping chunk payloads"); return false;
        }
    }
    width_ = int(w); height_ = int(h); chunkSize_ = int(cs); version_ = version;
    minHeight_ = minH; maxHeight_ = maxH; maxFlow_ = maxFlow;
    chunks_ = std::move(entries); bytes_.assign(data, data + size); return true;
}

bool TerrainAsset::loadChunk(int chunkX, int chunkY, TerrainChunkData &out, std::string *error) const {
    const auto it = std::find_if(chunks_.begin(), chunks_.end(), [&](const auto &e) { return e.chunkX == chunkX && e.chunkY == chunkY; });
    if (it == chunks_.end()) { fail(error, "terrain asset: chunk not found"); return false; }
    std::vector<uint8_t> raw;
    const uint8_t *stored = bytes_.data() + it->offset;
    if (it->compressed) { if (!unpackBits(stored, it->storedSize, it->rawSize, raw)) { fail(error, "terrain asset: invalid compressed chunk"); return false; } }
    else raw.assign(stored, stored + it->storedSize);
    if (raw.size() != it->rawSize || checksum(raw) != it->checksum) { fail(error, "terrain asset: chunk checksum mismatch"); return false; }
    out = {}; out.chunkX = chunkX; out.chunkY = chunkY; out.width = it->width; out.height = it->height; out.heights.resize(out.width, out.height);
    const size_t cells = size_t(out.width) * size_t(out.height); out.flowAccumulation.resize(cells); out.flowDirection.resize(cells); out.flowVectorX.resize(cells); out.flowVectorY.resize(cells); out.streamOrder.resize(cells); out.lakeDepth.resize(cells); out.temperature.resize(cells); out.moisture.resize(cells); out.rivers.resize(cells); out.biomes.resize(cells);
    const uint8_t *p = raw.data();
    for (size_t i = 0; i < cells; ++i) {
        const uint16_t qh = uint16_t(p[0]) | uint16_t(p[1] << 8); p += 2;
        out.heights.data()[i] = minHeight_ + (maxHeight_ - minHeight_) * (float(qh) / 65535.f);
        out.flowAccumulation[i] = (float(*p++) / 255.f) * maxFlow_;
        if (version_ >= 3) {
            const uint8_t encodedDirection = *p++;
            if (encodedDirection > 8) { fail(error, "terrain asset: invalid flow direction"); return false; }
            out.flowDirection[i] = int8_t(int(encodedDirection) - 1);
        } else out.flowDirection[i] = -1;
        if (version_ >= 4) {
            out.flowVectorX[i] = (float(*p++) / 255.f) * 2.f - 1.f;
            out.flowVectorY[i] = (float(*p++) / 255.f) * 2.f - 1.f;
        } else {
            static constexpr std::array<float, 8> vectorX{
                -0.70710678f, 0.f, 0.70710678f, -1.f, 1.f,
                -0.70710678f, 0.f, 0.70710678f};
            static constexpr std::array<float, 8> vectorY{
                -0.70710678f, -1.f, -0.70710678f, 0.f, 0.f,
                0.70710678f, 1.f, 0.70710678f};
            const int direction = int(out.flowDirection[i]);
            out.flowVectorX[i] = direction >= 0 ? vectorX[size_t(direction)] : 0.f;
            out.flowVectorY[i] = direction >= 0 ? vectorY[size_t(direction)] : 0.f;
        }
        out.streamOrder[i] = version_ >= 5 ? *p++ : 0;
        if (version_ >= 2)
            out.lakeDepth[i] = (float(*p++) / 255.f) * (maxHeight_ - minHeight_);
        else out.lakeDepth[i] = 0.f;
        out.temperature[i] = float(*p++) / 255.f; out.moisture[i] = float(*p++) / 255.f;
        out.rivers[i] = *p++; const uint8_t biome = *p++;
        if (biome > uint8_t(Biome::Wetland)) { fail(error, "terrain asset: invalid biome value"); return false; }
        out.biomes[i] = Biome(biome);
    }
    return true;
}

}  // namespace eve::procgen
