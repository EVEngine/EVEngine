#include "graphics/LightingMode.h"

#include "common/Capability.h"
#include "common/Diagnostic.h"
#include "common/GpuInfo.h"

#include <SDL2/SDL.h>

#include <cctype>
#include <cstdlib>
#include <string>

namespace eve::graphics {
namespace {

std::string toLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool containsInsensitive(const std::string& hay, const char* needle) {
    return toLower(hay).find(needle) != std::string::npos;
}

}  // namespace

LightingPreset suggestedLightingPreset() {
    if (const char* presetEnv = std::getenv("EVENGINE_LIGHTING_PRESET")) {
        const std::string p = toLower(presetEnv);
        if (p == "desktop") return LightingPreset::Desktop;
        if (p == "mobile") return LightingPreset::Mobile;
        if (p == "ci") return LightingPreset::Ci;
    }

    const char* platform = SDL_GetPlatform();
    if (platform) {
        const std::string os = toLower(platform);
        if (os.find("android") != std::string::npos || os.find("ios") != std::string::npos)
            return LightingPreset::Mobile;
    }

    if (auto* gpu = eve::cap::query<eve::caps::IGpuInfo>()) {
        if (gpu->gpuReady()) {
            const std::string type = toLower(gpu->gpuDeviceType());
            const std::string name = gpu->gpuName();
            if (type == "cpu" || containsInsensitive(name, "llvmpipe") || containsInsensitive(name, "lavapipe"))
                return LightingPreset::Ci;
        }
    }
    // Hosted CI often runs before GPU info is ready; prefer ForwardPlus there.
    if (std::getenv("CI") || std::getenv("GITHUB_ACTIONS") || std::getenv("EVENGINE_CI")) return LightingPreset::Ci;
    return LightingPreset::Desktop;
}

Result<LightingMode> lightingModeFromEnv() {
    const char* modeEnv = std::getenv("EVENGINE_LIGHTING_MODE");
    if (!modeEnv || !*modeEnv) {
        return Result<LightingMode>::failure(
            Diagnostic::error(DiagnosticCode::NotFound, "EVENGINE_LIGHTING_MODE unset", "graphics.LightingMode.env"));
    }
    const std::string m = toLower(modeEnv);
    if (m == "forwardplus" || m == "forward+") return Result<LightingMode>::success(LightingMode::ForwardPlus);
    if (m == "hybrid") return Result<LightingMode>::success(LightingMode::Hybrid);
    return Result<LightingMode>::failure(
        Diagnostic::error(DiagnosticCode::Unsupported, "EVENGINE_LIGHTING_MODE must be \"forwardPlus\" or \"hybrid\"",
                          "graphics.LightingMode.env"));
}

}  // namespace eve::graphics
