#pragma once
#include "asset/EvpackResourceReader.h"
#include "graphics/BlendMode.h"
#include "graphics/Color.h"
#include "graphics/PbrSurface.h"
namespace eve::asset_graphics::detail {
/** @brief Validated material value; image references own identity, GPU bindings initially empty. */
struct CookedMaterial {
    graphics::Color                         color{1, 1, 1, 1};
    float                                   metallic = 0, roughness = 1, alphaCutoff = .5f;
    bool                                    transparent = false, masked = false, doubleSided = false;
    graphics::BlendMode                     blend = graphics::BlendMode::Alpha;
    graphics::PbrSurface                    surface;
    std::array<std::optional<AssetRef>, 11> images;
};
/** @brief Decode material schemas 1/2 without allocating graphics resources.
 * Unknown additive fields are ignored; known fields are validated, with no coercion.
 * @thread Reentrant; reader is borrowed synchronously, no callbacks or retained pointers.
 * @return Validated owning material or a diagnostic; no externally visible mutation.
 */
[[nodiscard]] Result<CookedMaterial> readCookedMaterial(const asset::EvpackResourceReader& reader, const AssetRef& ref,
                                                        const asset::EvpackCapabilities& caps);
}  // namespace eve::asset_graphics::detail
