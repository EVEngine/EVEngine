#pragma once
#include <cstdint>
#include <string>

namespace eve {
namespace macosx
{

/**
 * Returns the filepath of the first detected love file in the Resources folder
 * in the main bundle
 * Returns an empty string if no file is found.
 **/
std::string getResources();

/**
 * Checks for drop-file events. Returns the filepath if an event occurred, or
 * an empty string otherwise.
 **/
std::string checkDropEvents();

/**
 * Returns the full path to the executable.
 **/
std::string getExecutablePath();

/**
 * Points the Vulkan loader at a MoltenVK bundled next to the executable
 * (../lib or ../Frameworks) and returns the directory that contains the
 * bundled loader + ICD manifest, or an empty string when no bundled Vulkan
 * layout is present.
 *
 * Sets VK_ICD_FILENAMES to the bundled MoltenVK_icd.json (unless the caller
 * already set it), so a packaged eve runs without the LunarG Vulkan SDK
 * installed. Call before creating any SDL Vulkan window.
 *
 * @return directory of the bundled libvulkan.1.dylib, or empty if not found.
 **/
std::string bootstrapBundledVulkan();

/**
 * Bounce the dock icon, if the app isn't in the foreground.
 **/
void requestAttention(bool continuous);

/**
 * Set the application (Dock) icon from tightly packed RGBA8 pixels.
 **/
void setIconRGBA(const uint8_t *rgba, int width, int height);

} // macosx
}  // namespace eve
