#include "ui/imgui/ImGuiBackend.h"

#include "common/Exception.h"
#include "common/config.h"
#include "graphics/Graphics.h"
#include "graphics/vulkan/Graphics.h"
#include "ui/Theme.h"
#include "vkbuilder.hpp"

#include <imgui.h>
#include <imgui_impl_sdl.h>
#include <imgui_impl_vulkan.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>


namespace eve::ui {
namespace {

void checkVk(VkResult err) {
    if (err == 0) return;
    throw eve::Exception("ImGui Vulkan error: VkResult = %d", int(err));
}

/** Base glyph size in logical px (before the DPI scale is applied). */
constexpr float kBaseFontSizePx = 16.f;

std::vector<const char *> regularFontCandidates() {
#if defined(_WIN32)
    static const std::string winDir = [] {
        const char *dir = getenv("WINDIR");
        return dir ? std::string(dir) : std::string("C:\\Windows");
    }();
    static const std::vector<std::string> paths = {
        winDir + "\\Fonts\\segoeui.ttf", winDir + "\\Fonts\\arial.ttf",
        winDir + "\\Fonts\\tahoma.ttf",   winDir + "\\Fonts\\msyh.ttc",
    };
#elif defined(__APPLE__)
    static const std::vector<std::string> paths = {
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/SFNS.ttf",
        "/Library/Fonts/Arial.ttf",
    };
#else
    static const std::vector<std::string> paths = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
    };
#endif
    std::vector<const char *> out;
    out.reserve(paths.size());
    for (const auto &p : paths) out.push_back(p.c_str());
    return out;
}

std::vector<const char *> iconFontCandidates() {
    static const std::vector<std::string> paths = {
        "fonts/FontAwesome.ttf",
        "test/fonts/FontAwesome.ttf",
    };
    std::vector<const char *> out;
    out.reserve(paths.size());
    for (const auto &p : paths) out.push_back(p.c_str());
    return out;
}

bool fileExists(const char *path) {
    if (!path || !*path) return false;
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}

}  // namespace

std::unique_ptr<UIBackend> createImGuiBackend() {
    return std::make_unique<ImGuiBackend>();
}

ImGuiBackend::~ImGuiBackend() { shutdown(); }

bool ImGuiBackend::init(SDL_Window *window, eve::graphics::Graphics *gfx) {
    if (initialized_) return true;
    if (!window || !gfx) return false;

    auto *vkg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    if (!vkg) return false;
    if (!vkg->getSwapchainRenderPass()) return false;

    gfx_ = gfx;
    window_ = window;
    uiScale_ = computeInitialScale();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    // Start from unified theme tokens (not a one-off ImGui palette).
    setThemeUiScale(uiScale_);
    applyThemeToImGui(globalTheme(), uiScale_);

    ImGui_ImplSDL2_InitForVulkan(window);

    auto &device = vkg->getDevice();
    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    };
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000 * uint32_t(sizeof(poolSizes) / sizeof(poolSizes[0]));
    poolInfo.poolSizeCount = uint32_t(sizeof(poolSizes) / sizeof(poolSizes[0]));
    poolInfo.pPoolSizes = poolSizes;

    VkDescriptorPool pool = VK_NULL_HANDLE;
    checkVk(vkCreateDescriptorPool(static_cast<VkDevice>(device.instance), &poolInfo, nullptr, &pool));
    imguiDescriptorPool_ = pool;

    uint32_t imageCount = vkg->getSwapchainImageCount();
    if (imageCount < 2) imageCount = 2;

    // The UI MSAA render pass must exist (and stay stable) before ImGui builds
    // its pipeline against it; the sample count must match that render pass.
    vkg->ensureUiColorResources();

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.Instance = static_cast<VkInstance>(vkg->getInstance().instance);
    initInfo.PhysicalDevice = static_cast<VkPhysicalDevice>(device.physical_device.instance);
    initInfo.Device = static_cast<VkDevice>(device.instance);
    initInfo.QueueFamily = device.get_queue_index(vkb::QueueType::graphics);
    initInfo.Queue = static_cast<VkQueue>(device.getQueue(vkb::QueueType::graphics));
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = pool;
    initInfo.MinImageCount = imageCount;
    initInfo.ImageCount = imageCount;
    initInfo.MSAASamples = static_cast<VkSampleCountFlagBits>(vkg->getUiMsaaSamples());
    initInfo.CheckVkResultFn = [](VkResult err) {
        if (err != 0) fprintf(stderr, "ImGui Vulkan: VkResult %d\n", int(err));
    };

    ImGui_ImplVulkan_Init(&initInfo, static_cast<VkRenderPass>(vkg->getUiMsaaRenderPass()));

    loadFonts();

    vkb::executeImmediately(device.instance, vkg->getUploadPool(),
                            device.getQueue(vkb::QueueType::graphics), [&](vk::CommandBuffer cb) {
                                ImGui_ImplVulkan_CreateFontsTexture(static_cast<VkCommandBuffer>(cb));
                            });
    ImGui_ImplVulkan_DestroyFontUploadObjects();
    fontsUploaded_ = true;

    gfx_->setPresentOverlay(&ImGuiBackend::presentOverlayThunk, this);

    initialized_ = true;
    return true;
}

void ImGuiBackend::shutdown() {
    if (!initialized_) return;
    if (frameOpen_) {
        ImGui::EndFrame();
        frameOpen_ = false;
    }
    if (gfx_) {
        if (gfx_->getPresentOverlayUser() == this) gfx_->setPresentOverlay(nullptr, nullptr);
    }
    auto *vkg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx_);
    if (vkg) {
        vkDeviceWaitIdle(static_cast<VkDevice>(vkg->getDevice().instance));
    }
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    if (imguiDescriptorPool_ && vkg) {
        vkDestroyDescriptorPool(static_cast<VkDevice>(vkg->getDevice().instance),
                                static_cast<VkDescriptorPool>(imguiDescriptorPool_), nullptr);
        imguiDescriptorPool_ = nullptr;
    }
    gfx_ = nullptr;
    window_ = nullptr;
    fontsUploaded_ = false;
    initialized_ = false;
}

void ImGuiBackend::processEvent(const SDL_Event *event) {
    if (!initialized_ || !event) return;
    ImGui_ImplSDL2_ProcessEvent(event);
}

void ImGuiBackend::newFrame() {
    if (!initialized_ || !window_) return;
    // present() may soft-skip on Android while the surface settles after
    // orientation change; close the previous ImGui frame so NewFrame is safe.
    if (frameOpen_) {
        ImGui::EndFrame();
        frameOpen_ = false;
    }
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL2_NewFrame(window_);
    ImGui::NewFrame();
    frameOpen_ = true;
}

void ImGuiBackend::applyScale(float scale) {
    if (!initialized_) return;
    scale = std::clamp(scale, 0.5f, 5.f);
    const bool changed = scale != uiScale_;
    uiScale_ = scale;
    setThemeUiScale(scale);
    // Re-apply design tokens so geometry stays in sync with theme + DPI.
    applyThemeToImGui(globalTheme(), scale);
    // The font atlas is rasterized at physical-pixel size, so a scale change
    // must re-rasterize it rather than rely on FontGlobalScale (which would
    // blur the glyphs again).
    if (changed) rebuildFonts();
}

void ImGuiBackend::setScale(float scale) {
    if (!initialized_) return;
    applyScale(scale);
}

float ImGuiBackend::computeInitialScale() const {
#if defined(EVENGINE_ANDROID) || defined(EVENGINE_IOS)
    // Readable size on phone/tablet (mdpi = 160).
    float ddpi = 160.f;
    if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) != 0 || ddpi < 1.f) ddpi = 320.f;
    float s = ddpi / 160.f;
    return std::clamp(s, 1.75f, 3.25f);
#else
    // Desktop: match the native window DPI scale (drawable / logical size).
    // Requires SDL_WINDOW_ALLOW_HIGHDPI to report a scale > 1 on HiDPI displays.
    int logicalW = 0, logicalH = 0, pixelW = 0, pixelH = 0;
    SDL_GetWindowSize(window_, &logicalW, &logicalH);
    SDL_GL_GetDrawableSize(window_, &pixelW, &pixelH);
    if (logicalW > 0 && pixelW > 0) {
        float s = float(pixelW) / float(logicalW);
        if (s > 0.f) return std::clamp(s, 1.f, 4.f);
    }
    return 1.f;
#endif
}

void ImGuiBackend::loadFonts() {
    ImGuiIO &io = ImGui::GetIO();
    ImFontAtlas *atlas = io.Fonts;
    if (!atlas) return;

    // Base glyph size in logical px at uiScale=1; physical size = base * uiScale.
    const float sizePx = kBaseFontSizePx * uiScale_;

    ImFontConfig cfg{};
    cfg.OversampleH = 2;
    cfg.OversampleV = 2;

    bool added = false;
    for (const char *path : regularFontCandidates()) {
        if (!fileExists(path)) continue;
        if (atlas->AddFontFromFileTTF(path, sizePx, &cfg)) {
            added = true;
            break;
        }
    }
    if (!added) atlas->AddFontDefault(&cfg);

    // Best-effort icon-font merge (FontAwesome, private-use glyphs). Missing
    // file is silently ignored so the default glyphs above still render.
    ImFontConfig iconCfg{};
    iconCfg.OversampleH = 2;
    iconCfg.OversampleV = 2;
    iconCfg.MergeMode = true;
    iconCfg.PixelSnapH = true;
    static const ImWchar iconRanges[] = {0xF000, 0xF8FF, 0};
    for (const char *path : iconFontCandidates()) {
        if (!fileExists(path)) continue;
        if (atlas->AddFontFromFileTTF(path, sizePx, &iconCfg, iconRanges)) break;
    }
}

void ImGuiBackend::rebuildFonts() {
    auto *vkg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx_);
    if (!vkg) return;
    loadFonts();
    ImGui_ImplVulkan_DestroyFontUploadObjects();
    auto &device = vkg->getDevice();
    vkb::executeImmediately(device.instance, vkg->getUploadPool(),
                            device.getQueue(vkb::QueueType::graphics),
                            [&](vk::CommandBuffer cb) {
                                ImGui_ImplVulkan_CreateFontsTexture(
                                    static_cast<VkCommandBuffer>(cb));
                            });
    ImGui_ImplVulkan_DestroyFontUploadObjects();
    fontsUploaded_ = true;
}

bool ImGuiBackend::wantCaptureMouse() const {
    if (!initialized_) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiBackend::wantCaptureKeyboard() const {
    if (!initialized_) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

void ImGuiBackend::renderDrawData(void *vkCommandBuffer) {
    if (!initialized_ || !vkCommandBuffer) return;
    ImGui::Render();
    frameOpen_ = false;
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
                                    static_cast<VkCommandBuffer>(vkCommandBuffer));
}

void ImGuiBackend::presentOverlayThunk(void *userdata, void *commandBuffer) {
    auto *self = static_cast<ImGuiBackend *>(userdata);
    if (!self) return;
    self->renderDrawData(commandBuffer);
}

}  // namespace eve::ui
