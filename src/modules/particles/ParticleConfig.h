#pragma once

#include "common/Json.h"
#include "common/Result.h"
#include "particles/ParticleEmitter.h"

#include <string>

namespace eve::particles {

/**
 * @brief Apply particle JSON config onto an emitter, including material texture resources.
 * Does not change buffer size or clear live particles.
 * @return Success, or InvalidArgument when the root is missing / not an object.
 */
[[nodiscard]] eve::Result<void> applyConfigDocument(ParticleEmitter* emitter, eve::json::Value root);

/** @brief Parse JSON text and apply. */
bool applyConfigText(ParticleEmitter *emitter, const std::string &json, std::string *error = nullptr);

/**
 * @brief Read path via Filesystem, apply, and bind Resource.path + modtime for hot reload.
 * Returns false if file missing / invalid JSON.
 */
bool loadConfigFile(ParticleEmitter *emitter, const std::string &path, std::string *error = nullptr);

/** @brief Re-read Resource.path if set; updates modtime. */
bool reloadConfigFile(ParticleEmitter *emitter, std::string *error = nullptr);

}  // namespace eve::particles
