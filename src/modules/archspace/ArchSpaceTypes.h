#pragma once

#include "common/Result.h"

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
