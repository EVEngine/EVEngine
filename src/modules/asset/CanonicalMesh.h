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
    std::vector<std::uint32_t>                  indices;
};
/** @brief Allocation limits checked before decoding any vertex arrays. UV count is byte-budget bounded. */
struct CanonicalMeshLimits {
    std::uint32_t maximumVertices     = 4'000'000;
    std::uint32_t maximumIndices      = 12'000'000;
    std::uint64_t maximumDecodedBytes = 512ull * 1024ull * 1024ull;
};
/** @brief Decode canonical EVMESH binary v1 or v2 into independently owned arrays.
 * @param bytes Borrowed immutable input valid for this synchronous call only.
 * @param limits Bounds checked before allocating staging data.
 * @return Complete snapshot, or a checked format/range/budget failure; no partial result escapes.
 * @thread Worker-safe and reentrant; no IO, callbacks, backend calls or global mutation.
 * @details v1 is compatibility-only. Both versions decode to the same canonical multi-UV snapshot.
 */
[[nodiscard]] Result<CanonicalMeshData> decodeCanonicalMesh(std::span<const std::uint8_t> bytes,
                                                            const CanonicalMeshLimits&    limits = {});
}  // namespace eve::asset
