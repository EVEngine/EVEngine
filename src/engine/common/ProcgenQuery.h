#pragma once

#include "common/Export.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace eve {

/** @brief Procedural generation query surface (provided by the procgen module). */
class EVENGINE_API IProcgenQuery {
public:
    static constexpr const char* capabilityName = "IProcgenQuery";

    virtual ~IProcgenQuery() = default;

    virtual std::vector<std::string> algorithms() = 0;
    virtual std::vector<std::string> meshRecipes() = 0;
    virtual std::vector<std::string> textureRecipes() = 0;
    virtual std::vector<std::string> pbrRecipes() = 0;

    /** @brief Generate a grid; returns grid JSON ("" + err on failure). */
    virtual std::string generateMap(const std::string& algorithm, int width, int height,
                                    uint32_t seed, const std::vector<std::pair<std::string, std::string>>& params,
                                    std::string* err) = 0;
    /** @brief Build a mesh recipe; returns a JSON summary ("" + err on failure). */
    virtual std::string buildMesh(const std::string& recipe, uint32_t seed, int width, int height,
                                  int depth, std::string* err) = 0;
};

}  // namespace eve
