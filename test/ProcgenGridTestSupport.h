#pragma once

#include "procgen/Grid2D.h"
#include "procgen/Procgen.h"

#include "map/TileLayer.h"

#include "zeroerr/unittest.h"

#include <string>
#include <utility>

namespace eve::test_support {

/**
 * @brief Applies a test-owned grid through Procgen's handle-only map boundary.
 *
 * The generator tests keep their algorithm output in a local `Grid2D`, while
 * `Procgen::applyToLayer` deliberately consumes a module-owned generation
 * handle. This helper makes that ownership transition explicit: allocate the
 * module slot, resolve a synchronous borrowed view, copy the test grid, save
 * and inspect the apply result, then release the slot and inspect release.
 *
 * @param procgen Procgen module that owns the palette and operation.
 * @param source Local test grid to copy into module-owned storage.
 * @param palette Palette name used for the application.
 * @param layer Destination tile layer; its lifetime must cover this call.
 * @remarks Test-only helper. It never returns a borrowed pointer or retains a
 *          grid handle after the synchronous operation.
 */
inline void applyGridToLayer(eve::procgen::Procgen& procgen,
                             const eve::procgen::Grid2D& source,
                             const std::string& palette,
                             eve::map::TileLayer& layer) {
    auto created = eve::procgen::Procgen::newGridHandle(source.getWidth(), source.getHeight());
    const bool createdOk = created.ok();
    REQUIRE(createdOk);
    if (!createdOk) return;

    const auto handle = std::move(created).takeValue();
    auto view = eve::procgen::Procgen::resolve(handle);
    const bool resolved = view.isBound();
    if (resolved) *view = source;

    if (!resolved) {
        auto releaseResult = eve::procgen::Procgen::release(handle);
        const bool released = releaseResult.ok();
        REQUIRE(released);
        REQUIRE(false);
        return;
    }

    // Keep the operation Result alive until it has been observed before the
    // handle is released; this also avoids ZeroErr evaluating it twice.
    auto applyResult = procgen.applyToLayer(handle, palette, layer);
    const bool applied = applyResult.ok();
    auto releaseResult = eve::procgen::Procgen::release(handle);
    const bool released = releaseResult.ok();
    REQUIRE(applied);
    REQUIRE(released);
}

}  // namespace eve::test_support
