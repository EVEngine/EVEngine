#pragma once

#include "particles/ParticleEmitter.h"

#include <string>

namespace eve::data {
class JsonDocument;
}

namespace eve::particles {

/**
 * @brief Apply particle JSON config onto an emitter (Config + Draw + Resource.texturePath).
 * Does not change buffer size or clear live particles.
 * Returns false on invalid / missing object root.
 */
bool applyConfigDocument(ParticleEmitter *emitter, data::JsonDocument *doc);

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
