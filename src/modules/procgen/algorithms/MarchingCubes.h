#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/ParamSchema.h"
#include "procgen/Params.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen {

/**
 * @brief Classic Marching Cubes (Lorensen & Cline) over a regular scalar volume.
 * Density >= isolevel is treated as solid; normals point toward empty space.
 *
 * `density` is row-major: index = x + y * nx + z * nx * ny, size nx*ny*nz.
 * Cell (x,y,z) spans corners [x..x+1]×[y..y+1]×[z..z+1] (so loops are nx-1 etc.).
 */
bool marchingCubes(const float *density, int nx, int ny, int nz, float isolevel, MeshBuild &out,
                   std::string *error = nullptr);

/** @brief Fill a density volume from a named field recipe (sphere / noise / terrain / torus). */
bool fillDensityField(const Params &params, std::vector<float> &density, int &nx, int &ny, int &nz,
                      std::string &error);

/** @brief Build mesh from Params (field + resolution + isolevel). */
bool generateMarchingCubesMesh(const Params &params, MeshBuild &out, std::string &error);

/**
 * @brief Build the dual of a subdivided icosahedron. The result is a closed planet made
 * of hexagonal cells plus the twelve pentagons required by spherical topology.
 * Params: radius (1), subdivisions (2), tileInset (0.06, in [0, 0.5)).
 */
bool generateHexPlanetMesh(const Params &params, MeshBuild &out, std::string &error);

using MeshRecipeFn = std::function<bool(const Params &params, MeshBuild &out, std::string &error)>;

class MeshRecipeRegistry {
public:
    /** @brief Access the process-wide mesh recipe registry. @return Registry instance. */
    static MeshRecipeRegistry &instance();

    /** @brief Register a recipe without metadata. @param id Recipe id. @param fn Generator callback. */
    void registerRecipe(const std::string &id, MeshRecipeFn fn);
    /** @brief Register a recipe with reusable metadata. @param descriptor Recipe schema. @param fn Generator callback. */
    void registerRecipe(RecipeDescriptor descriptor, MeshRecipeFn fn);
    bool has(const std::string &id) const;
    bool generate(const std::string &id, const Params &params, MeshBuild &out,
                  std::string &error) const;
    std::vector<std::string> list() const;
    /** @brief Look up recipe metadata. @param id Recipe id. @return Registry-owned schema or nullptr. */
    const RecipeDescriptor *descriptor(const std::string &id) const;
    /** @brief Fill missing values from metadata. @param id Recipe id. @param params Values to update. @return False for an unknown recipe. */
    bool applyDefaults(const std::string &id, Params &params) const;

    void registerBuiltins();

private:
    struct Entry {
        MeshRecipeFn fn;
        RecipeDescriptor descriptor;
    };
    MeshRecipeRegistry() = default;
    std::unordered_map<std::string, Entry> recipes_;
    bool builtinsRegistered_ = false;
};

}  // namespace eve::procgen
