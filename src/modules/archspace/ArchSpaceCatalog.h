#pragma once
#include "common/Export.h"


#include "archspace/ArchSpaceTypes.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace eve::archspace {

/** @brief One parametric furniture part in catalog-local metres (Y-up). */
struct CatalogPart {
    Vec3        center{};             ///< Local center relative to the item origin.
    Vec3        size{0.5, 0.5, 0.5};  ///< Full extents along local X / Y / Z.
    std::string suffix;               ///< Appended to the item id for bake primitive ids.
};

/** @brief Built-in catalog entry resolved from an item `catalogId`. */
struct CatalogEntry {
    std::string              id;
    std::string              displayName;
    std::vector<CatalogPart> parts;
};

/**
 * @brief Resolve a built-in furniture catalog entry.
 * @return Entry when known; empty optional for unknown ids (bake falls back to a marker).
 */
[[nodiscard]] EVENGINE_API_WORLD std::optional<CatalogEntry> lookupCatalog(std::string_view catalogId);

/** @brief Stable list of built-in catalog ids for tools and inspector UIs. */
[[nodiscard]] EVENGINE_API_WORLD std::vector<std::string> listCatalogIds();

/**
 * @brief Append yaw-oriented catalog geometry into a mesh bake.
 * @param bake Destination mesh.
 * @param itemId Stable item node id used as the primitive id prefix.
 * @param catalogId Catalog key; unknown ids emit a default marker box.
 * @param position World/level position of the item origin.
 * @param yawDegrees Rotation about +Y in degrees.
 */
void appendCatalogItem(MeshBake& bake, const std::string& itemId, const std::string& catalogId, const Vec3& position,
                       double yawDegrees);

/**
 * @brief Bake one wall with merged opening cavities and explicit reveal liners.
 *
 * Overlapping openings are merged along the wall parameter before carving. Reveal
 * primitives use `*.reveal.*` ids so hosts can distinguish cavity liners from structure.
 * This is a parametric boolean (segment solids + reveal faces), not IFC/CSG BREP.
 */
void appendWallWithOpenings(MeshBake& bake, const Node& wall, double elevation,
                            const std::vector<const Node*>& openings);

}  // namespace eve::archspace
