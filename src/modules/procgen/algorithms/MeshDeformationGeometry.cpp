#include "procgen/algorithms/MeshDeformationGeometry.h"

#include "procgen/ParamSchema.h"
#include "procgen/Params.h"
#include "procgen/algorithms/MarchingCubes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>

namespace eve::procgen {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float hash01(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return static_cast<float>(value & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

void addFace(MeshBuild& mesh, float ax, float ay, float az, float bx, float by, float bz, float cx, float cy, float cz,
             float dx, float dy, float dz, float nx, float ny, float nz) {
    const auto base = static_cast<std::uint32_t>(mesh.getVertexCount());
    mesh.addVertex(ax, ay, az, nx, ny, nz, 0.f, 0.f);
    mesh.addVertex(bx, by, bz, nx, ny, nz, 1.f, 0.f);
    mesh.addVertex(cx, cy, cz, nx, ny, nz, 1.f, 1.f);
    mesh.addVertex(dx, dy, dz, nx, ny, nz, 0.f, 1.f);
    mesh.addTriangle(base, base + 1, base + 2);
    mesh.addTriangle(base, base + 2, base + 3);
}

bool generateExtendedPlane(const Params& params, MeshBuild& output, std::string& error) {
    const int   sx        = params.getInt("segmentsX", 16);
    const int   sz        = params.getInt("segmentsZ", 16);
    const float width     = params.getFloat("width", 4.f);
    const float depth     = params.getFloat("depth", 4.f);
    const float curvature = params.getFloat("curvature", 0.f);
    const bool  sharp     = params.getBool("sharpNormals", false);
    if (sx < 1 || sx > 1024 || sz < 1 || sz > 1024 || !std::isfinite(width) || !std::isfinite(depth) ||
        !std::isfinite(curvature) || width <= 0.f || depth <= 0.f || (sx + 1LL) * (sz + 1LL) > 1000000LL) {
        error = "mesh.extendedPlane: invalid size, curvature, segment count, or vertex budget";
        return false;
    }
    output.setActiveGroup("surface");
    auto position = [&](int x, int z) {
        const float u  = static_cast<float>(x) / static_cast<float>(sx);
        const float v  = static_cast<float>(z) / static_cast<float>(sz);
        const float px = (u - 0.5f) * width;
        const float pz = (v - 0.5f) * depth;
        const float py = curvature * (1.f - std::cos(u * kPi * 2.f)) * 0.5f;
        return std::array<float, 5>{px, py, pz, u, v};
    };
    if (sharp) {
        for (int z = 0; z < sz; ++z)
            for (int x = 0; x < sx; ++x) {
                const auto a = position(x, z), b = position(x + 1, z), c = position(x + 1, z + 1),
                           d = position(x, z + 1);
                addFace(output, a[0], a[1], a[2], d[0], d[1], d[2], c[0], c[1], c[2], b[0], b[1], b[2], 0.f, 1.f, 0.f);
            }
    } else {
        for (int z = 0; z <= sz; ++z)
            for (int x = 0; x <= sx; ++x) {
                const auto  p     = position(x, z);
                const float slope = curvature * kPi / width * std::sin(p[3] * kPi * 2.f);
                const float inv   = 1.f / std::sqrt(1.f + slope * slope);
                output.addVertex(p[0], p[1], p[2], -slope * inv, inv, 0.f, p[3], p[4]);
            }
        for (int z = 0; z < sz; ++z)
            for (int x = 0; x < sx; ++x) {
                const std::uint32_t a = static_cast<std::uint32_t>(x + z * (sx + 1));
                const std::uint32_t b = a + 1, d = a + static_cast<std::uint32_t>(sx + 1), c = d + 1;
                output.addTriangle(a, d, c);
                output.addTriangle(a, c, b);
            }
    }
    output.setMeta("generator", "mesh.extendedPlane");
    output.setMeta("normalMode", sharp ? "sharp" : "smooth");
    return true;
}

bool generateHexGrid(const Params& params, MeshBuild& output, std::string& error) {
    const int   width = params.getInt("columns", 8), height = params.getInt("rows", 8);
    const float radius = params.getFloat("radius", 0.5f), randomHeight = params.getFloat("randomHeight", 0.f);
    const float baseHeight = params.getFloat("height", 0.2f);
    const bool  planar = params.getBool("planar", false), flipFaces = params.getBool("flipFaces", false);
    if (width < 1 || width > 256 || height < 1 || height > 256 || !std::isfinite(radius) || radius <= 0.f ||
        !std::isfinite(baseHeight) || !std::isfinite(randomHeight) || baseHeight < 0.f || randomHeight < 0.f) {
        error = "mesh.hexGrid: invalid dimensions, radius, or height";
        return false;
    }
    output.setActiveGroup("hexagons");
    const std::uint32_t seed = params.getSeed();
    for (int row = 0; row < height; ++row)
        for (int column = 0; column < width; ++column) {
            const float cx     = radius * 1.5f * static_cast<float>(column);
            const float cz     = radius * 1.73205080757f * (static_cast<float>(row) + (column & 1 ? 0.5f : 0.f));
            const float h      = planar ? 0.f : baseHeight + randomHeight * hash01(seed + row * 4099u + column * 131u);
            const auto  center = static_cast<std::uint32_t>(output.getVertexCount());
            output.addVertex(cx, h, cz, 0.f, flipFaces ? -1.f : 1.f, 0.f, 0.5f, 0.5f);
            for (int side = 0; side < 6; ++side) {
                const float angle = kPi / 3.f * static_cast<float>(side);
                output.addVertex(cx + std::cos(angle) * radius, h, cz + std::sin(angle) * radius, 0.f,
                                 flipFaces ? -1.f : 1.f, 0.f, 0.5f + std::cos(angle) * 0.5f,
                                 0.5f + std::sin(angle) * 0.5f);
            }
            for (int side = 0; side < 6; ++side) {
                const auto a = center + 1 + static_cast<std::uint32_t>(side);
                const auto b = center + 1 + static_cast<std::uint32_t>((side + 1) % 6);
                if (flipFaces)
                    output.addTriangle(center, a, b);
                else
                    output.addTriangle(center, b, a);
            }
        }
    output.setMeta("generator", "mesh.hexGrid");
    output.setMeta("layout", "flatTopOffset");
    return true;
}

}  // namespace

void registerMeshDeformationGeometryRecipes(MeshRecipeRegistry& registry) {
    RecipeDescriptor plane{"mesh.extendedPlane", "Extended Plane", "Mesh Deformation", {}};
    plane.params.push_back(ParamDescriptor::floating("width", "Width", 4.f, 0.01f, 10000.f, 0.01f));
    plane.params.push_back(ParamDescriptor::floating("depth", "Depth", 4.f, 0.01f, 10000.f, 0.01f));
    plane.params.push_back(ParamDescriptor::integer("segmentsX", "X Segments", 16, 1, 1024));
    plane.params.push_back(ParamDescriptor::integer("segmentsZ", "Z Segments", 16, 1, 1024));
    plane.params.push_back(ParamDescriptor::floating("curvature", "Curvature", 0.f, -1000.f, 1000.f, 0.01f));
    plane.params.push_back(ParamDescriptor::boolean("sharpNormals", "Sharp Normals", false));
    registry.registerRecipe(std::move(plane), generateExtendedPlane);

    RecipeDescriptor hex{"mesh.hexGrid", "Hexagon Grid", "Mesh Deformation", {}};
    hex.params.push_back(ParamDescriptor::integer("columns", "Columns", 8, 1, 256));
    hex.params.push_back(ParamDescriptor::integer("rows", "Rows", 8, 1, 256));
    hex.params.push_back(ParamDescriptor::floating("radius", "Radius", 0.5f, 0.01f, 1000.f, 0.01f));
    hex.params.push_back(ParamDescriptor::floating("height", "Base Height", 0.2f, 0.f, 1000.f, 0.01f));
    hex.params.push_back(ParamDescriptor::floating("randomHeight", "Random Height", 0.f, 0.f, 1000.f, 0.01f));
    hex.params.push_back(ParamDescriptor::boolean("planar", "Planar", false));
    hex.params.push_back(ParamDescriptor::boolean("flipFaces", "Flip Faces", false));
    registry.registerRecipe(std::move(hex), generateHexGrid);
}

}  // namespace eve::procgen
