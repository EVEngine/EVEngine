#pragma once

#include <string>

namespace eve {
namespace webgpu_platform {

/**
 * WebGPU platform services shared by the browser (Emscripten/WASM) and native
 * Dawn builds. Mirrors the interface of platform/ios and platform/android so
 * the engine code can call into it uniformly.
 *
 * On the browser the game directory is the preloaded VFS mount ("/game"),
 * appdata is an IndexedDB-backed persistent dir ("/persist"), and the
 * "executable" is the page itself. On native builds the helpers resolve to
 * filesystem paths relative to the CWD.
 */

/**
 * Root directory containing the game assets (main.nut / config.nut / data).
 * Browser: "/game" (--preload-file). Native: "." unless EVENGINE_WEBGPU_GAME_DIR
 * is set.
 **/
std::string getGameDirectory();

/**
 * Directory where files should be stored (saves / user data).
 * Browser: "/persist" (IndexedDB). Native: "appdata" under the CWD.
 **/
std::string getAppdataDirectory();

/**
 * Home directory. Browser: the VFS root "/". Native: CWD.
 **/
std::string getHomeDirectory();

/**
 * Returns the full path to the "executable".
 * Browser: the page URL. Native: argv[0]-style best effort.
 **/
std::string getExecutablePath();

/**
 * Opens the specified URL in a new browser tab / system default browser.
 * Browser: window.open. Native: no-op returning false.
 **/
bool openURL(const std::string &url);

/**
 * Causes devices with vibration support to vibrate for about 0.5 seconds.
 * Browser: navigator.vibrate. Native: no-op.
 **/
void vibrate();

/**
 * Returns whether another application is playing audio (browser: no-op).
 **/
bool hasBackgroundMusic();

/**
 * Flush pending browser-side work (device requests, message queue).
 * On Emscripten this processes the browser message queue so requestDevice
 * callbacks fire on the main thread when PROXY_TO_PTHREAD is disabled.
 **/
void pumpEvents();

}  // namespace webgpu_platform
}  // namespace eve
