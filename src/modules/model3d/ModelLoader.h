#pragma once

#include "model3d/Model3D.h"

#include <string>

namespace eve::model3d {

/**
 * @brief Build a deterministic ResourceManager cache key for `path` + `options`.
 * @param path VFS path of the model file.
 * @param options Decode options; each flag becomes a `?param=0|1` entry.
 * @return The cache key, e.g. "sofa.obj?triangulate=1&normals=1&...".
 */
std::string modelCacheKey(const std::string &path, const ModelLoadOptions &options);

/**
 * @brief Parse the options back out of a model cache key (inverse of modelCacheKey).
 * @param key A key produced by modelCacheKey.
 * @return The decoded options (defaults for missing parameters).
 */
ModelLoadOptions modelOptionsFromKey(const std::string &key);

}  // namespace eve::model3d
