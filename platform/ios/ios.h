#pragma once

#include "common/Math.h"

#include <string>

struct SDL_Window;

namespace eve {
namespace ios {

/**
 * Gets the filepath of the first detected file. The main .app Bundle is
 * searched first, and then the app's Documents folder.
 **/
std::string getResources(bool &fused);

/**
 * Directory containing the bundled example/game assets (Resources/game).
 **/
std::string getGameDirectory();

/**
 * Gets the directory path where files should be stored.
 **/
std::string getAppdataDirectory();

/**
 * Get the home directory (on iOS, this really means the app's sandbox dir.)
 **/
std::string getHomeDirectory();

/**
 * Opens the specified URL with the default program associated with the URL's
 * scheme.
 **/
bool openURL(const std::string &url);

/**
 * Returns the full path to the executable.
 **/
std::string getExecutablePath();

/**
 * Causes devices with vibration support to vibrate for about 0.5 seconds.
 **/
void vibrate();

/**
 * Enable mix mode (e.g. with background music apps) and playback with a muted device.
 **/
bool setAudioMixWithOthers(bool mixEnabled);

/**
 * Returns whether another application is playing audio.
 **/
bool hasBackgroundMusic();

/**
 * Registers notifications to handle and restore audio interruptions
 **/
void initAudioSessionInterruptionHandler();
void destroyAudioSessionInterruptionHandler();

/**
 * Gets the area in the window that is safe for UI to render to (not covered by
 * the status bar, notch, etc.)
 **/
Rect getSafeArea(SDL_Window *window);

}  // namespace ios
}  // namespace eve
