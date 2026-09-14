#pragma once

/**
 * @file RTSContent.h
 * @brief Atomic RTS content-pack import into the canonical definition registry.
 */

#include "common/Result.h"
#include "definitions/Definitions.h"

#include <cstddef>
#include <string_view>

namespace eve::rts {

/** @brief Receipt for one atomically imported RTS content pack. */
struct ContentImportReceipt {
    std::size_t inserted = 0;
    std::size_t replaced = 0;
};

/**
 * @brief Validates legacy-compatible RTS content packs and publishes their records to Definitions.
 *
 * The loader owns no definition map. Unit, building, weapon, upgrade, effect,
 * ability, and damage-table records remain authoritative in the caller's
 * DefinitionRegistry. Failed imports restore the registry snapshot captured
 * before the first mutation.
 */
class RTSContentLoader {
public:
    /** @brief Parse, cross-validate, normalize weapons, and atomically import one JSON pack. */
    [[nodiscard]] static Result<ContentImportReceipt> load(definitions::DefinitionRegistry& registry,
                                                            std::string_view json);
};

}  // namespace eve::rts
