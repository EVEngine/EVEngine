#pragma once
#include <cstdint>
#include <map>
#include <span>
#include <vector>
#include "common/Result.h"
namespace eve::asset {
/** @brief Owning CPU mesh snapshot; UV arrays are packed ST pairs indexed by source set number.
 * No pointers into the source archive are retained. Each array has one authoritative owner.
 */
struct CanonicalMeshData {
    std::vector<float>                          positions;
    std::vector<float>                          normals;
    std::map<std::uint32_t, std::vector<float>> texcoords;
    /** @brief Optional tightly packed RGBA vertex colors in linear space. */
    std::vector<float>                          colors;
    std::vector<std::uint32_t>                  indices;
};
/** @brief Allocation limits checked before decoding any vertex arrays. UV count is byte-budget bounded. */
struct CanonicalMeshLimits {
    std::uint32_t maximumVertices     = 4'000'000;
    std::uint32_t maximumIndices      = 12'000'000;
    std::uint64_t maximumDecodedBytes = 512ull * 1024ull * 1024ull;
};
/**
 * @brief Encode an owned CPU mesh as canonical EVMESH binary version 3.
 * @param mesh Borrowed immutable arrays consumed synchronously.
 * @param limits Bounds checked before allocating the output.
 * @return Complete binary blob, or a structured validation/budget failure.
 * @thread Worker-safe and reentrant; performs no IO or callbacks.
 */
[[nodiscard]] Result<std::vector<std::uint8_t>> encodeCanonicalMesh(
    const CanonicalMeshData& mesh, const CanonicalMeshLimits& limits = {});
/** @brief Decode canonical EVMESH binary v1, v2, or v3 into independently owned arrays.
 * @param bytes Borrowed immutable input valid for this synchronous call only.
 * @param limits Bounds checked before allocating staging data.
 * @return Complete snapshot, or a checked format/range/budget failure; no partial result escapes.
 * @thread Worker-safe and reentrant; no IO, callbacks, backend calls or global mutation.
 * @details v1 and v2 are compatibility-only. v3 adds an optional linear RGBA vertex-color stream.
 */
[[nodiscard]] Result<CanonicalMeshData> decodeCanonicalMesh(std::span<const std::uint8_t> bytes,
                                                            const CanonicalMeshLimits&    limits = {});
}  // namespace eve::asset
