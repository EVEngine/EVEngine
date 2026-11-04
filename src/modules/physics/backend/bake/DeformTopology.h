#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>
#include "common/Export.h"
#include "common/Result.h"
namespace eve::physics {
/** @brief Explicit primitive arity; connectivity retains order and triangle winding. */
enum class DeformPrimitive { Segments, Triangles };
/** @brief Fixed per-vertex texture coordinates, independent of animated positions. */
struct DeformUV {
    float u = 0, v = 0;
};
/** @brief Immutable connectivity and UV ownership for fixed-topology deformation playback.
 * @ownership Copies share frozen value storage; vertex indices define stable identities within this topology.
 * @lifetime Spans borrow this topology's shared storage and expire when its final owning copy is destroyed.
 * @thread Const access is reentrant; preparation retains no source pointers, solver, GPU objects or callbacks.
 * @remarks A changed index order, winding, vertex count or UV produces a different topology identity.
 */
class EVENGINE_API_BACKENDS DeformTopology {
public:
    /** @brief Validate bounded connectivity and freeze it with a deterministic content identity.
     * @param primitive Segment pairs or triangle triples; unknown primitive kinds are rejected.
     * @param vertexCount Number of stable indexed vertices, in [1, 1048576].
     * @param indices Ordered endpoints/corners; at most 6291456 entries, no repeated vertex within a primitive.
     * @param uv Either empty or exactly one finite UV per vertex; empty explicitly means no UV channel.
     * @return Complete owning topology, or failure without publishing partial state.
     * @cost Linear in indices and UVs; prepare once per topology, outside playback.
     */
    [[nodiscard]] static Result<DeformTopology> prepare(DeformPrimitive primitive, std::size_t vertexCount,
                                                        std::vector<std::uint32_t> indices, std::vector<DeformUV> uv);
    /** @brief Primitive interpretation of the index stream. */
    [[nodiscard]] DeformPrimitive primitive() const noexcept;
    /** @brief Number of stable vertex slots. */
    [[nodiscard]] std::size_t vertexCount() const noexcept;
    /** @brief Borrow immutable ordered connectivity, bounded by this shared storage lifetime. */
    [[nodiscard]] std::span<const std::uint32_t> indices() const noexcept;
    /** @brief Borrow immutable UVs; empty means no UV channel. */
    [[nodiscard]] std::span<const DeformUV> uv() const noexcept;
    /** @brief Borrow the precomputed noncryptographic content key; no per-frame hashing or allocation. */
    [[nodiscard]] std::string_view key() const noexcept;

private:
    struct Data;
    explicit DeformTopology(std::shared_ptr<const Data> data);
    std::shared_ptr<const Data> data_;
};
}  // namespace eve::physics
