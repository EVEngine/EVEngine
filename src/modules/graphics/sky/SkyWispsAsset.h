#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include "common/Export.h"
#include "common/Result.h"

namespace eve::graphics {
struct SkyWispsLayer;
/** @brief Immutable owned sky mesh and textures, loaded from a pack or generated procedurally.
 * @details Reads eve.sky-wisps/1 through /7 and eve.sky-mesh/1 through the existing VFS.
 * Unknown fields, unsupported versions and malformed resources fail without publication.
 * No GPU resources, callbacks or provider references are retained.
 * @thread Load on the game thread with an initialized filesystem; immutable reads may be shared. */
class EVENGINE_API_BACKENDS SkyWispsAsset {
public:
    /** @brief Read and decode the complete asset before publishing owned data.
     * @param manifestPath VFS manifest path; resource paths resolve relative to it.
     * @return Immutable candidate or structured failure, with no partially observable state.
     * @cost File reads and mesh/texture decoding; amortize over sky preparation, never call per frame. */
    [[nodiscard]] static Result<SkyWispsAsset> load(const std::string& manifestPath);
    /** @brief Generate a complete independent sky using a named procedural-v1 seed.
     * @param seed Explicit cloud/star/moon random stream seed; does not consume simulation RNG.
     * @return Immutable owned candidate or structured failure; no filesystem/GPU state is touched.
     * @details Repeatable on one platform; floating-point mesh/noise may vary slightly across platforms.
     * @thread Worker-safe, synchronous, no callbacks. Upload borrows remain valid for this asset lifetime.
     * @cost Generates bounded mesh and mipmapped textures once; amortize over sky preparation, never per frame. */
    [[nodiscard]] static Result<SkyWispsAsset> generate(uint32_t seed);
    /** @brief Borrow upload data until the last copy of this asset is destroyed. */
    [[nodiscard]] const SkyWispsLayer& layer() const noexcept;

private:
    struct Impl;
    explicit SkyWispsAsset(std::shared_ptr<const Impl> data);
    std::shared_ptr<const Impl> data_;
};
}  // namespace eve::graphics
