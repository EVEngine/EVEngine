#pragma once

#include "common/Export.h"
#include "common/Result.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace eve {

/** @brief Procedural generation query surface (provided by the procgen module). */
class EVENGINE_API_FOUNDATION_INLINE IProcgenQuery {
public:
    static constexpr const char* capabilityName = "IProcgenQuery";

    /** @brief I procgen query. */
    virtual ~IProcgenQuery() = default;

    /** @brief Algorithms. */
    virtual std::vector<std::string> algorithms() = 0;
    /** @brief Mesh recipes. */
    virtual std::vector<std::string> meshRecipes() = 0;
    /** @brief Texture recipes. */
    virtual std::vector<std::string> textureRecipes() = 0;
    /** @brief Pbr recipes. */
    virtual std::vector<std::string> pbrRecipes() = 0;

    /**
     * @brief Generate a grid and serialize it as JSON.
     * @return Grid JSON on success; Unsupported/Failed with diagnostics otherwise.
     */
    [[nodiscard]] virtual Result<std::string> generateMap(
        const std::string& algorithm, int width, int height, uint32_t seed,
        const std::vector<std::pair<std::string, std::string>>& params) = 0;
    /**
     * @brief Build a mesh recipe and return a JSON vertex/triangle summary.
     * @return Summary JSON on success; Unsupported/Failed with diagnostics otherwise.
     */
    [[nodiscard]] virtual Result<std::string> buildMesh(const std::string& recipe, uint32_t seed, int width, int height,
                                                        int depth) = 0;
};

}  // namespace eve
