#include "common/config.h"
#if !defined(EVENGINE_WEBGPU)
#include <SDL2/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include "common/Module.h"
#include "common/Runtime.h"
#include "graphics/Graphics.h"
#include "graphics/vulkan/Graphics.h"
#include "window/Window.h"
#include "zeroerr/assert.h"
#include "zeroerr/unittest.h"

namespace {
struct GameDirectory {
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("eve-vsync-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    explicit GameDirectory(const std::string &fields) {
        std::filesystem::create_directory(path);
        std::ofstream(path / "config.nut") << "config = {width=96,height=96,modules=[]" << fields << "};\n";
        std::filesystem::current_path(path);
    }
    ~GameDirectory() {
        std::filesystem::current_path(previous);
        // Preserve the config and diagnostic log: the process logger can keep
        // eve.log open until process exit on Windows.
    }
};
struct BootResult {
    bool             enabled;
    int              count;
    VkPresentModeKHR mode;
    VkPresentModeKHR expectedMode;
    bool             deferred;
};
BootResult bootConfig(const std::string &fields, bool enabled, bool editor = false) {
    const auto    sourcePath = std::filesystem::path(EVENGINE_SOURCE_DIR) / "src/scripts/load.nut";
    std::ifstream input(sourcePath, std::ios::binary);
    std::string   source(std::istreambuf_iterator<char>(input), {});
    const auto    end = source.find("// Node-style async");
    REQUIRE_NE(end, std::string::npos);
    source.resize(end);
    GameDirectory game(fields);
    auto         *win = eve::window::Window::create();
    struct Close {
        eve::window::Window *win;
        ~Close() { win->close(); }
    } close{win};
    eve::Runtime runtime(2048, ssq::Libs::ALL);
    eve::ModuleManager::expose(runtime);
    runtime.runSource("eve.moduleList <- [{slot=\"win\",cls=\"Window\"},{slot=\"gfx\",cls=\"Graphics\"}]\n");
    runtime.runSource(source, "load.nut");
    auto *gfx = eve::graphics::Graphics::create();
    REQUIRE_EQ(gfx->isVSync(), enabled);
    REQUIRE_EQ(gfx->getVSyncCount(), enabled ? 1 : 0);
    runtime.runSource(enabled ? "if(gfx.getVSyncCount()!=1) throw \"vsync not enabled\";"
                              : "if(gfx.getVSyncCount()!=0) throw \"vsync not disabled\";");
    const bool deferred = editor && !win->isOpen();
    if (editor) {
        REQUIRE(!win->isOpen());
        runtime.runSource("boot_create_window();");
    }
    REQUIRE(win->isOpen());
    SDL_HideWindow(static_cast<SDL_Window *>(win->getHandle()));
    auto      *backend     = static_cast<eve::graphics::vulkan::Graphics *>(gfx);
    const auto initialMode = backend->getSwapchain().present_mode;
    SDL_SetWindowSize(static_cast<SDL_Window *>(win->getHandle()), 112, 96);
    gfx->setViewportSize(112, 96, 112, 96);
    gfx->present();
    REQUIRE_EQ(backend->getSwapchain().present_mode, initialMode);
    const auto  selectedMode = static_cast<VkPresentModeKHR>(backend->getSwapchain().present_mode);
    const auto &device       = backend->getDevice();
    const auto  modes        = device.physical_device->getSurfacePresentModesKHR(device.surface);
    const auto  desired      = enabled ? vk::PresentModeKHR::eMailbox : vk::PresentModeKHR::eImmediate;
    const bool  supported    = std::find(modes.begin(), modes.end(), desired) != modes.end();
    if (supported) REQUIRE_EQ(selectedMode, static_cast<VkPresentModeKHR>(desired));
    if (enabled) REQUIRE_NE(selectedMode, VK_PRESENT_MODE_IMMEDIATE_KHR);
    std::fprintf(stderr, "VSYNC_CONFIG enabled=%d count=%d actualPresentMode=%d\n", enabled, gfx->getVSyncCount(),
                 int(selectedMode));
    const bool mailbox  = std::find(modes.begin(), modes.end(), vk::PresentModeKHR::eMailbox) != modes.end();
    const auto expected = supported ? desired : (mailbox ? vk::PresentModeKHR::eMailbox : vk::PresentModeKHR::eFifo);
    return {gfx->isVSync(), gfx->getVSyncCount(), selectedMode, static_cast<VkPresentModeKHR>(expected), deferred};
}
}  // namespace
TEST_CASE("scriptConfig.vsyncDefaultsEnabled") {
    const auto result = bootConfig("", true);
    REQUIRE(result.enabled);
    REQUIRE_EQ(result.count, 1);
    REQUIRE_EQ(result.mode, result.expectedMode);
}
TEST_CASE("scriptConfig.vsyncExplicitTrue") {
    const auto result = bootConfig(",vsync=true", true);
    REQUIRE(result.enabled);
    REQUIRE_EQ(result.count, 1);
    REQUIRE_EQ(result.mode, result.expectedMode);
}
TEST_CASE("scriptConfig.vsyncFalseUsesUnsynchronizedPresent") {
    const auto result = bootConfig(",vsync=false", false);
    REQUIRE(!result.enabled);
    REQUIRE_EQ(result.count, 0);
    REQUIRE_EQ(result.mode, result.expectedMode);
}
TEST_CASE("scriptConfig.vsyncAppliedBeforeEditorWindow") {
    const auto result = bootConfig(",vsync=false,editorHost=true", false, true);
    REQUIRE(!result.enabled);
    REQUIRE_EQ(result.count, 0);
    REQUIRE_EQ(result.mode, result.expectedMode);
    REQUIRE(result.deferred);
}

TEST_CASE("scriptConfig.vsyncRejectsNonBoolean") {
    bool rejected = false;
    try {
        bootConfig(",vsync=0", true);
    } catch (const eve::ScriptException &error) {
        rejected = std::string(error.what()).find("config.vsync must be a bool") != std::string::npos;
    }
    REQUIRE(rejected);
}

#endif
