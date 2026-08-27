#pragma once

#include "common/Result.h"

namespace eve::schema {

/**
 * @brief Registers the engine-owned envelope and definition metadata schemas.
 *
 * The helper is owned by the schema module so common data types do not depend
 * on Snapshot, GameEvent, or Definitions. Registration is idempotent when
 * the complete standard set is already present and never replaces a caller's
 * exact version.
 * @return Success, or a structured conflict/failure without partial standard
 *         registrations.
 */
[[nodiscard]] EVENGINE_API eve::Result<void> registerStandardSchemas();

}  // namespace eve::schema
