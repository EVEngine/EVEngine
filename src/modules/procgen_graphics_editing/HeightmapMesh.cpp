#include "procgen_graphics_editing/HeightmapMesh.h"

#include "common/Module.h"
#include "graphics/Graphics.h"
#include "procgen/heightmap/Heightmap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace eve::procgen_graphics_editing {
namespace {

struct HeightmapArrays {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> texcoords;
    std::vector<std::uint32_t> indices;
};

void buildSmooth(const procgen::Heightmap& heightmap, float cellSize, float heightScale,
                 HeightmapArrays& output) {
    const int width  = heightmap.getWidth();
    const int height = heightmap.getHeight();
    if (width < 2 || height < 2 || cellSize == 0.0F) return;
    output.positions.reserve(static_cast<std::size_t>(width * height * 3));
    output.normals.reserve(static_cast<std::size_t>(width * height * 3));
    output.texcoords.reserve(static_cast<std::size_t>(width * height * 2));
    output.indices.reserve(static_cast<std::size_t>((width - 1) * (height - 1) * 6));
    auto sample = [&](int x, int y) {
        return heightmap.height(std::clamp(x, 0, width - 1), std::clamp(y, 0, height - 1));
    };
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            output.positions.insert(output.positions.end(),
                                    {static_cast<float>(x) * cellSize,
                                     heightmap.height(x, y) * heightScale,
                                     static_cast<float>(y) * cellSize});
            float nx = -(sample(x + 1, y) - sample(x - 1, y)) * 0.5F * heightScale / cellSize;
            float ny = 1.0F;
            float nz = -(sample(x, y + 1) - sample(x, y - 1)) * 0.5F * heightScale / cellSize;
            const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (length > 1.0e-8F) {
                nx /= length;
                ny /= length;
                nz /= length;
            }
            output.normals.insert(output.normals.end(), {nx, ny, nz});
            output.texcoords.insert(output.texcoords.end(),
                                    {static_cast<float>(x) / static_cast<float>(width - 1),
                                     static_cast<float>(y) / static_cast<float>(height - 1)});
        }
    }
    for (int y = 0; y < height - 1; ++y) {
        for (int x = 0; x < width - 1; ++x) {
            const std::uint32_t i00 = static_cast<std::uint32_t>(y * width + x);
            const std::uint32_t i10 = i00 + 1U;
            const std::uint32_t i01 = i00 + static_cast<std::uint32_t>(width);
            const std::uint32_t i11 = i01 + 1U;
            output.indices.insert(output.indices.end(), {i00, i01, i10, i10, i01, i11});
        }
    }
}

void addFlatTriangle(HeightmapArrays& output, float cellSize, float heightScale,
                     float textureWidth, float textureHeight, float ax, float ay, float ah,
                     float bx, float by, float bh, float cx, float cy, float ch) {
    const float p0x = ax * cellSize, p0y = ah * heightScale, p0z = ay * cellSize;
    const float p1x = bx * cellSize, p1y = bh * heightScale, p1z = by * cellSize;
    const float p2x = cx * cellSize, p2y = ch * heightScale, p2z = cy * cellSize;
    float nx = (p1y - p0y) * (p2z - p0z) - (p1z - p0z) * (p2y - p0y);
    float ny = (p1z - p0z) * (p2x - p0x) - (p1x - p0x) * (p2z - p0z);
    float nz = (p1x - p0x) * (p2y - p0y) - (p1y - p0y) * (p2x - p0x);
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (length > 1.0e-8F) {
        nx /= length;
        ny /= length;
        nz /= length;
    }
    const std::uint32_t base = static_cast<std::uint32_t>(output.positions.size() / 3U);
    output.positions.insert(output.positions.end(), {p0x, p0y, p0z, p1x, p1y, p1z, p2x, p2y, p2z});
    output.normals.insert(output.normals.end(), {nx, ny, nz, nx, ny, nz, nx, ny, nz});
    output.texcoords.insert(output.texcoords.end(),
                            {ax / textureWidth, ay / textureHeight, bx / textureWidth,
                             by / textureHeight, cx / textureWidth, cy / textureHeight});
    output.indices.insert(output.indices.end(), {base, base + 1U, base + 2U});
}

void buildFlat(const procgen::Heightmap& heightmap, float cellSize, float heightScale,
               HeightmapArrays& output) {
    const int width  = heightmap.getWidth();
    const int height = heightmap.getHeight();
    if (width < 2 || height < 2) return;
    const float textureWidth  = static_cast<float>(width - 1);
    const float textureHeight = static_cast<float>(height - 1);
    for (int y = 0; y < height - 1; ++y) {
        for (int x = 0; x < width - 1; ++x) {
            const float h00 = heightmap.height(x, y);
            const float h10 = heightmap.height(x + 1, y);
            const float h01 = heightmap.height(x, y + 1);
            const float h11 = heightmap.height(x + 1, y + 1);
            addFlatTriangle(output, cellSize, heightScale, textureWidth, textureHeight,
                            static_cast<float>(x), static_cast<float>(y), h00,
                            static_cast<float>(x), static_cast<float>(y + 1), h01,
                            static_cast<float>(x + 1), static_cast<float>(y), h10);
            addFlatTriangle(output, cellSize, heightScale, textureWidth, textureHeight,
                            static_cast<float>(x + 1), static_cast<float>(y), h10,
                            static_cast<float>(x), static_cast<float>(y + 1), h01,
                            static_cast<float>(x + 1), static_cast<float>(y + 1), h11);
        }
    }
}

HeightmapArrays build(const procgen::Heightmap& heightmap, float cellSize, float heightScale,
                      bool smoothNormals) {
    HeightmapArrays result;
    if (smoothNormals)
        buildSmooth(heightmap, cellSize, heightScale, result);
    else
        buildFlat(heightmap, cellSize, heightScale, result);
    return result;
}

}  // namespace

editing::Result<graphics::Mesh*> createHeightmapMesh(procgen::Heightmap* heightmap, float cellSize,
                                                     float heightScale, bool smoothNormals) {
    auto* graphics = ModuleManager::getInstance<eve::graphics::Graphics>("Graphics");
    if (!graphics || !heightmap)
        return eve::editing::failed<graphics::Mesh*>(
            editing::Status::Unsupported, editing::RuleId("procgen.preview.provider-absent"),
            "Heightmap preview requires Graphics and a source heightmap");
    HeightmapArrays arrays = build(*heightmap, cellSize, heightScale, smoothNormals);
    if (arrays.indices.empty())
        return eve::editing::failed<graphics::Mesh*>(
            editing::Status::Rejected, editing::RuleId("procgen.preview.empty-heightmap"),
            "Heightmap preview requires a valid grid and cell size");
    auto* mesh = graphics->newMeshFromArrays(
        arrays.positions.data(), arrays.normals.data(), arrays.texcoords.data(),
        static_cast<int>(arrays.positions.size() / 3U), arrays.indices.data(),
        static_cast<int>(arrays.indices.size()));
    if (!mesh)
        return eve::editing::failed<graphics::Mesh*>(
            editing::Status::Failed, editing::RuleId("procgen.preview.mesh-create-failed"),
            "Graphics rejected the heightmap preview mesh");
    return eve::editing::applied<graphics::Mesh*>(mesh);
}

editing::Result<void> updateHeightmapMesh(graphics::Mesh* mesh, graphics::Graphics* graphics,
                                          procgen::Heightmap* heightmap, float cellSize,
                                          float heightScale, bool smoothNormals) {
    if (!mesh || !graphics || !heightmap)
        return eve::editing::failed<void>(
            editing::Status::Rejected, editing::RuleId("procgen.preview.invalid-update"),
            "Heightmap preview update requires mesh, Graphics and heightmap");
    HeightmapArrays arrays = build(*heightmap, cellSize, heightScale, smoothNormals);
    if (arrays.indices.empty())
        return eve::editing::failed<void>(
            editing::Status::Rejected, editing::RuleId("procgen.preview.empty-heightmap"),
            "Heightmap preview requires a valid grid and cell size");
    if (!graphics->updateMeshVertices(mesh, arrays.positions.data(), arrays.normals.data(),
                                      arrays.texcoords.data(),
                                      static_cast<int>(arrays.positions.size() / 3U), nullptr, 0))
        return eve::editing::failed<void>(
            editing::Status::Failed, editing::RuleId("procgen.preview.mesh-update-failed"),
            "Graphics rejected the heightmap preview update");
    return eve::editing::applied<void>();
}

}  // namespace eve::procgen_graphics_editing
