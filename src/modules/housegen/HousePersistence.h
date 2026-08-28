#pragma once

/**
 * @file HousePersistence.h
 * @brief procgen-backed persistence and hot-reload identity for house layouts.
 *
 * housegen consumes procgen's deterministic identity protocol (BuildKey) and
 * its owning artifact store (ArtifactStore) instead of maintaining a second
 * persistence format. A generated HouseLayout is wrapped into a procgen
 * artifact whose typed payload is the ground-footprint Grid2D and whose
 * metadata carries the full layout JSON; snapshot/restore then flows through
 * the same store used by other procgen products.
 */

#include "common/Identity.h"
#include "common/Result.h"
#include "housegen/HouseGenTypes.h"

#include <string>

namespace eve::procgen {
class ArtifactStore;
struct GeneratedArtifact;
}  // namespace eve::procgen

namespace eve::housegen {

class HouseLayout;

/**
 * @brief Deterministic canonical identity text for a generation request.
 * @param request The generation inputs that produced (or would produce) a layout.
 * @return A stable canonical string capturing every input that changes output.
 * @remarks Two requests that differ in any generation input yield different
 *          text, so a request-derived key is safe for hot-reload comparison.
 */
[[nodiscard]] std::string houseRequestBuildKeyText(const HouseRequest &request);

/**
 * @brief Deterministic canonical identity text for a generated layout result.
 * @param layout The resolved layout (styles and all instances resolved).
 * @return A stable canonical string; equal layouts yield equal text.
 */
[[nodiscard]] std::string houseLayoutBuildKeyText(const HouseLayout &layout);

/**
 * @brief Wrap a generated layout as a procgen artifact and publish it atomically.
 * @param store The owning procgen artifact store to publish into.
 * @param layout The generated layout to persist.
 * @return The published artifact identity, or a structured diagnostic.
 * @ownership The store owns the published artifact; housegen retains no pointer.
 */
[[nodiscard]] eve::Result<eve::ArtifactId> publishHouseLayout(procgen::ArtifactStore &store,
                                                              const HouseLayout      &layout);

/**
 * @brief Read a layout back from a previously published housegen artifact.
 * @param artifact The artifact previously returned by publishHouseLayout.
 * @return The restored layout, or a structured parse/validation diagnostic.
 * @remarks A failure leaves no partially parsed layout behind.
 */
[[nodiscard]] eve::Result<HouseLayout> restoreHouseLayout(const procgen::GeneratedArtifact &artifact);

}  // namespace eve::housegen