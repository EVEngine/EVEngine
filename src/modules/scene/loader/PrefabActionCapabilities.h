#pragma once

namespace eve::sceneloader {

/** @brief Register the SceneLoader-backed prefab action provider. */
void registerPrefabActionCapabilities();

/** @brief Release all provider-owned prefab instances during SceneLoader shutdown. */
void shutdownPrefabActionCapabilities();

}  // namespace eve::sceneloader
