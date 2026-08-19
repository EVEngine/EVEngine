#pragma once

#include <cstddef>
#include <string>

namespace eve {
namespace android {

bool directoryExists(const char *path);
bool mkdir(const char *path);
bool createStorageDirectories();

/** Immersive fullscreen (hide system UI). */
void setImmersive(bool immersive);

/**
 * Love2D-style fused game helpers. MVP returns false / safe no-ops so
 * Filesystem::setSource can fall through to mounting a real directory.
 */
bool checkFusedGame(void **gameLoveIO);
bool initializeVirtualArchive();
void deinitializeVirtualArchive();
std::string getSelectedGameFile();
bool loadGameArchiveToMemory(const char *filename, char **outPtr, size_t *outSize);
void freeGameArchiveMemory(void *ptr);

/** Absolute path to the native library / "executable" (best-effort). */
std::string getExecutablePath();

/**
 * Writable directory used to stage remote hot-reload downloads
 * (<internal storage>/hotreload). Created on first use.
 */
std::string getHotReloadDirectory();

}  // namespace android
}  // namespace eve
