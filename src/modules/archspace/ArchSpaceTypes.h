#pragma once

#include "common/Result.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace eve::archspace {

/** @brief Stable node kind discriminator for the architectural document. */
enum class NodeKind : std::uint8_t { Site = 0, Building, Level, Wall, Slab, Zone, Item, Opening };

/** @brief Opening semantic used by doors and windows hosted on walls. */
enum class OpeningKind : std::uint8_t { Door = 0, Window };

/** @brief Metres on the level XZ plane. */
struct Vec2 {
    double x = 0.0;
    double z = 0.0;
};

/** @brief Metres in world/level space. */
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

/** @brief One authored architectural node. Only fields for the active kind are meaningful. */
struct Node {
    std::string              id;
    NodeKind                 kind = NodeKind::Site;
    std::string              parentId;
    std::string              name;
    std::vector<std::string> children;

    // level
    double elevation = 0.0;
    double height    = 3.0;

    // wall
    Vec2   start{};
    Vec2   end{};
    double thickness = 0.2;

    // slab / zone
    std::vector<Vec2> polygon;
    double            slabThickness = 0.2;

    // item
    Vec3        position{};
    double      yawDegrees = 0.0;
    std::string catalogId;

    // opening
    OpeningKind openingKind   = OpeningKind::Door;
    double      t             = 0.5;
    double      width         = 0.9;
    double      openingHeight = 2.1;
    double      sill          = 0.0;
};

/** @brief Renderer-neutral triangle mesh rebuilt from the document. */
struct MeshBake {
    std::vector<Vec3>          positions;
    std::vector<std::uint32_t> indices;
    std::vector<std::string>   primitiveIds;
};

/**
 * @brief Interleaved GPU-upload arrays derived from MeshBake.
 *
 * positions/normals/uvs are xyz / xyz / uv packed floats. indices are triangles.
 */
struct MeshArrays {
    std::vector<float>         positions;
    std::vector<float>         normals;
    std::vector<float>         uvs;
    std::vector<std::uint32_t> indices;

    [[nodiscard]] int vertexCount() const noexcept {
        return static_cast<int>(positions.size() / 3);
    }
    [[nodiscard]] int indexCount() const noexcept { return static_cast<int>(indices.size()); }
    [[nodiscard]] int triangleCount() const noexcept { return indexCount() / 3; }
};

/** @brief Convert a MeshBake into flat arrays with per-triangle normals. */
[[nodiscard]] inline MeshArrays toMeshArrays(const MeshBake& bake) {
    MeshArrays out;
    out.positions.reserve(bake.positions.size() * 3);
    out.normals.reserve(bake.positions.size() * 3);
    out.uvs.reserve(bake.positions.size() * 2);
    out.indices = bake.indices;
    for (const Vec3& p : bake.positions) {
        out.positions.push_back(static_cast<float>(p.x));
        out.positions.push_back(static_cast<float>(p.y));
        out.positions.push_back(static_cast<float>(p.z));
        out.normals.push_back(0.f);
        out.normals.push_back(1.f);
        out.normals.push_back(0.f);
        out.uvs.push_back(static_cast<float>(p.x));
        out.uvs.push_back(static_cast<float>(p.z));
    }
    for (std::size_t i = 0; i + 2 < bake.indices.size(); i += 3) {
        const std::uint32_t i0 = bake.indices[i], i1 = bake.indices[i + 1], i2 = bake.indices[i + 2];
        if (i0 >= bake.positions.size() || i1 >= bake.positions.size() || i2 >= bake.positions.size()) continue;
        const Vec3& a = bake.positions[i0];
        const Vec3& b = bake.positions[i1];
        const Vec3& c = bake.positions[i2];
        Vec3        n{ (b.y - a.y) * (c.z - a.z) - (b.z - a.z) * (c.y - a.y),
                       (b.z - a.z) * (c.x - a.x) - (b.x - a.x) * (c.z - a.z),
                       (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) };
        const double len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
        if (len > 1e-8) {
            n.x /= len;
            n.y /= len;
            n.z /= len;
        } else {
            n = {0, 1, 0};
        }
        for (std::uint32_t idx : {i0, i1, i2}) {
            out.normals[idx * 3 + 0] = static_cast<float>(n.x);
            out.normals[idx * 3 + 1] = static_cast<float>(n.y);
            out.normals[idx * 3 + 2] = static_cast<float>(n.z);
        }
    }
    return out;
}

/**
 * @brief Stable wire name for a node kind.
 * @ownership Non-owning pointer to a static string literal; do not free.
 * @lifetime Valid for the process lifetime.
 */
[[nodiscard]] inline const char* nodeKindName(NodeKind kind) noexcept {
    switch (kind) {
        case NodeKind::Site: return "site";
        case NodeKind::Building: return "building";
        case NodeKind::Level: return "level";
        case NodeKind::Wall: return "wall";
        case NodeKind::Slab: return "slab";
        case NodeKind::Zone: return "zone";
        case NodeKind::Item: return "item";
        case NodeKind::Opening: return "opening";
    }
    return "site";
}

/**
 * @brief Parse a node kind wire name.
 * @return Parsed kind, or InvalidArgument when the text is unknown.
 */
[[nodiscard]] inline eve::Result<NodeKind> parseNodeKind(const std::string& text) noexcept {
    if (text == "site") return eve::Result<NodeKind>::success(NodeKind::Site);
    if (text == "building") return eve::Result<NodeKind>::success(NodeKind::Building);
    if (text == "level") return eve::Result<NodeKind>::success(NodeKind::Level);
    if (text == "wall") return eve::Result<NodeKind>::success(NodeKind::Wall);
    if (text == "slab") return eve::Result<NodeKind>::success(NodeKind::Slab);
    if (text == "zone") return eve::Result<NodeKind>::success(NodeKind::Zone);
    if (text == "item") return eve::Result<NodeKind>::success(NodeKind::Item);
    if (text == "opening") return eve::Result<NodeKind>::success(NodeKind::Opening);
    return eve::Result<NodeKind>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "unknown ArchSpace node kind", "kind", {}, "archspace.kind"));
}

/**
 * @brief Stable wire name for an opening kind.
 * @ownership Non-owning pointer to a static string literal; do not free.
 * @lifetime Valid for the process lifetime.
 */
[[nodiscard]] inline const char* openingKindName(OpeningKind kind) noexcept {
    return kind == OpeningKind::Window ? "window" : "door";
}

/**
 * @brief Parse an opening kind wire name.
 * @return Parsed kind, or InvalidArgument when the text is unknown.
 */
[[nodiscard]] inline eve::Result<OpeningKind> parseOpeningKind(const std::string& text) noexcept {
    if (text == "door") return eve::Result<OpeningKind>::success(OpeningKind::Door);
    if (text == "window") return eve::Result<OpeningKind>::success(OpeningKind::Window);
    return eve::Result<OpeningKind>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                    "unknown ArchSpace opening kind", "openingKind", {},
                                                                    "archspace.opening"));
}

}  // namespace eve::archspace
