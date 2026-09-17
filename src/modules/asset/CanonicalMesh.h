#pragma once
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>
#include "common/Result.h"
namespace eve::asset {
/** @brief Owning float vertex attribute; one to four components per vertex. */
struct CanonicalMeshAttribute {
    std::uint32_t      components = 0;
    std::vector<float> values;
};
/** @brief Owning CPU mesh snapshot; UV arrays are packed ST pairs indexed by source set number.
 * No pointers into the source archive are retained. Each array has one authoritative owner.
 */
struct CanonicalMeshData {
    std::vector<float>                          positions;
    std::vector<float>                          normals;
    std::vector<float>                          colors;
    std::map<std::uint32_t, std::vector<float>> texcoords;
    std::vector<std::uint32_t>                  indices;
    std::map<std::string, CanonicalMeshAttribute> attributes;
};
/** @brief Allocation limits checked before decoding any vertex arrays. UV count is byte-budget bounded. */
struct CanonicalMeshLimits {
    std::uint32_t maximumVertices     = 4'000'000;
    std::uint32_t maximumIndices      = 12'000'000;
    std::uint64_t maximumDecodedBytes = 512ull * 1024ull * 1024ull;
};
/** @brief Decode canonical EVMESH binary v1, v2 or v3 into independently owned arrays.
 * @param bytes Borrowed
 * immutable input valid for this synchronous call only.
 * @param limits Bounds checked before allocating staging data.
 * @return Complete snapshot, or a checked format/range/budget failure; no partial result escapes.
 * @thread Worker-safe and reentrant; no IO, callbacks, backend calls or global mutation.
 * @details Older versions are compatibility inputs. V3 additionally retains named float attributes.
 */
[[nodiscard]] Result<CanonicalMeshData> decodeCanonicalMesh(std::span<const std::uint8_t> bytes,
                                                            const CanonicalMeshLimits&    limits = {});
/** @brief Encode an owning snapshot to EVMESH v3, or v2 when no named attributes exist.
 * Borrows input synchronously;
 * worker-safe, reentrant, without callbacks or retained pointers.
 * @return Complete bytes or checked
 * validation/budget failure; no partial output escapes.
 */
[[nodiscard]] Result<std::vector<std::uint8_t>> encodeCanonicalMesh(const CanonicalMeshData&   mesh,
                                                                    const CanonicalMeshLimits& limits = {});
}  // namespace eve::asset
