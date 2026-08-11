#pragma once

#include "procgen/MeshBuild.h"
#include "procgen/Params.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::procgen {

/**
 * Classic Marching Cubes (Lorensen & Cline) over a regular scalar volume.
 * Density >= isolevel is treated as solid; normals point toward empty space.
 *
 * `density` is row-major: index = x + y * nx + z * nx * ny, size nx*ny*nz.
 * Cell (x,y,z) spans corners [x..x+1]×[y..y+1]×[z..z+1] (so loops are nx-1 etc.).
 */
bool marchingCubes(const float *density, int nx, int ny, int nz, float isolevel, MeshBuild &out,
                   std::string *error = nullptr);

/** Fill a density volume from a named field recipe (sphere / noise / terrain / torus). */
bool fillDensityField(const Params &params, std::vector<float> &density, int &nx, int &ny, int &nz,
                      std::string &error);

/** Build mesh from Params (field + resolution + isolevel). */
bool generateMarchingCubesMesh(const Params &params, MeshBuild &out, std::string &error);

using MeshRecipeFn = std::function<bool(const Params &params, MeshBuild &out, std::string &error)>;

class MeshRecipeRegistry {
public:
    static MeshRecipeRegistry &instance();

    void registerRecipe(const std::string &id, MeshRecipeFn fn);
    bool has(const std::string &id) const;
    bool generate(const std::string &id, const Params &params, MeshBuild &out,
                  std::string &error) const;
    std::vector<std::string> list() const;

    void registerBuiltins();

private:
    MeshRecipeRegistry() = default;
    std::unordered_map<std::string, MeshRecipeFn> recipes_;
    bool builtinsRegistered_ = false;
};

}  // namespace eve::procgen
