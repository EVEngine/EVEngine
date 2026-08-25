#include "ui/imgui/ImGuiBackend.h"
#include "ui/Theme.h"

#include "common/Exception.h"
#include "common/StartupTiming.h"
#include "common/config.h"
#include "graphics/Graphics.h"

#include <imgui.h>
#include <imgui_impl_sdl.h>

#ifdef EVENGINE_WEBGPU
#include "graphics/webgpu/Graphics.h"
#include "imgui_impl_wgpu.h"
#else
#include "graphics/vulkan/Graphics.h"
#include <imgui_impl_vulkan.h>
#include <vulkan/vulkan.h>
#include <SDL2/SDL_vulkan.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>


namespace eve::ui {
namespace {

#ifndef EVENGINE_WEBGPU
void checkVk(VkResult err) {
    if (err == 0) return;
    throw eve::Exception("ImGui Vulkan error: VkResult = %d", int(err));
}
#endif

/** Base glyph size in logical px (before the DPI scale is applied). */
constexpr float kBaseFontSizePx = 17.f;

/** U+4E2D "中": probe used to verify a font actually rasterizes CJK glyphs. */
constexpr ImWchar kCjkProbeCodepoint = 0x4E2D;
/** U+F002 search: probe used to verify the editor icon font was merged. */
constexpr ImWchar kIconProbeCodepoint = 0xF002;

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
#elif defined(EVENGINE_ANDROID)
    static const std::vector<std::string> paths = {
        "/system/fonts/Roboto-Regular.ttf",
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf",
    };
#else
    static const std::vector<std::string> paths = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/arphic/uming.ttc",
    };
#endif
    std::vector<const char *> out;
    out.reserve(paths.size());
    for (const auto &p : paths) out.push_back(p.c_str());
    return out;
}

// System fonts that actually contain CJK glyphs, tried in order when the
// regular candidates above (Segoe UI / DejaVu Sans / Helvetica, ...) turn out
// to be Latin-only. Each platform's first entry is the most common CJK font.
std::vector<const char *> cjkFontCandidates() {
#if defined(_WIN32)
    static const std::string winDir = [] {
        const char *dir = getenv("WINDIR");
        return dir ? std::string(dir) : std::string("C:\\Windows");
    }();
    static const std::vector<std::string> paths = {
        winDir + "\\Fonts\\msyh.ttc",   // Microsoft YaHei (simplified + traditional)
        winDir + "\\Fonts\\simhei.ttf", // SimHei
        winDir + "\\Fonts\\simsun.ttc", // SimSun
        winDir + "\\Fonts\\Deng.ttf",   // DengXian
        winDir + "\\Fonts\\msjh.ttc",   // Microsoft JhengHei (traditional)
    };
#elif defined(__APPLE__)
    static const std::vector<std::string> paths = {
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc",
        "/System/Library/Fonts/Supplemental/Songti.ttc",
        "/Library/Fonts/Arial Unicode.ttf",
    };
#elif defined(EVENGINE_ANDROID)
    static const std::vector<std::string> paths = {
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/DroidSansFallback.ttf",
        "/system/fonts/NotoSansSC-Regular.otf",
    };
#else
    static const std::vector<std::string> paths = {
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/truetype/arphic/uming.ttc",
    };
#endif
    std::vector<const char *> out;
    out.reserve(paths.size());
    for (const auto &p : paths) out.push_back(p.c_str());
    return out;
}

std::vector<std::string> iconFontCandidates() {
    std::vector<std::string> paths;
    if (const char *overridePath = getenv("EVENGINE_ICON_FONT")) {
        if (*overridePath) paths.emplace_back(overridePath);
    }
    if (char *basePath = SDL_GetBasePath()) {
        const std::filesystem::path base(basePath);
        // Build-tree layout: <build>/src/engine/eve.exe -> <build>/share/eve/fonts.
        paths.push_back(
            (base / "../../share/eve/fonts/FontAwesome.ttf").lexically_normal().string());
        // Installed SDK layouts used by the packaged executable.
        paths.push_back(
            (base / "../../../share/eve/fonts/FontAwesome.ttf").lexically_normal().string());
        paths.push_back(
            (base / "../share/eve/fonts/FontAwesome.ttf").lexically_normal().string());
        SDL_free(basePath);
    }
    paths.emplace_back("share/eve/fonts/FontAwesome.ttf");
    paths.emplace_back("fonts/FontAwesome.ttf");
    paths.emplace_back("test/fonts/FontAwesome.ttf");
    return paths;
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
    StartupStage initStage("ui: ImGui backend init (first frame)");

    gfx_ = gfx;
    window_ = window;
    dpiScale_ = computeDpiScale();
    uiScale_ = computeInitialScale();

    IMGUI_CHECKVERSION();
    ctx_ = ::ImGui::CreateContext();
    // Start from unified theme tokens (not a one-off ImGui palette).
    setThemeDpiScale(dpiScale_);
    setThemeUiScale(uiScale_);
    applyThemeToImGui(globalTheme(), uiScale_);

    ImGui_ImplSDL2_InitForVulkan(window);

#ifdef EVENGINE_WEBGPU
    auto *wgg = dynamic_cast<eve::graphics::webgpu::Graphics *>(gfx);
    if (!wgg) return false;
    ImGui_ImplWGPU_Init(wgg->getDevice().Get(), 2, wgg->getSurfaceFormat());
    loadFonts();
    ImGui_ImplWGPU_CreateFontsTexture();
    fontsUploaded_ = true;
    checkFontCoverage();
#else
    auto *vkg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx);
    if (!vkg) return false;
    if (!vkg->getSwapchainRenderPass()) return false;

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

    {
        StartupStage fontStage("  ui: font load + atlas upload");
        loadFonts();

        vkb::executeImmediately(device.instance, vkg->getUploadPool(),
                                device.getQueue(vkb::QueueType::graphics), [&](vk::CommandBuffer cb) {
                                    ImGui_ImplVulkan_CreateFontsTexture(static_cast<VkCommandBuffer>(cb));
                                });
        ImGui_ImplVulkan_DestroyFontUploadObjects();
        fontsUploaded_ = true;
        checkFontCoverage();
    }
#endif

    gfx_->setPresentOverlay(&ImGuiBackend::presentOverlayThunk, this);
    // The ImGui context + Vulkan pipeline are bound to the native window. When
    // the window is destroyed, tear down so the next init() rebuilds against a
    // fresh window — even if SDL hands back the same pointer.
    gfx_->addWindowDestroyedCallback(&ImGuiBackend::windowDestroyedThunk, this);

    initialized_ = true;
    return true;
}

void ImGuiBackend::windowDestroyedThunk(void *userdata) {
    auto *self = static_cast<ImGuiBackend *>(userdata);
    if (self) self->shutdown();
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
#ifdef EVENGINE_WEBGPU
    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
#else
    auto *vkg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx_);
    if (vkg) {
        vkDeviceWaitIdle(static_cast<VkDevice>(vkg->getDevice().instance));
    }
    // The ImGui backend data lives on the context this backend created. It may
    // not be the currently active context (headless tests switch contexts), so
    // explicitly select it before tearing down ImGui_Impl* state.
    if (ctx_) {
        ::ImGuiContext *prev = ::ImGui::GetCurrentContext();
        ::ImGuiContext *mine = ctx_;
        ::ImGui::SetCurrentContext(mine);
        ::ImGui_ImplVulkan_Shutdown();
        ::ImGui_ImplSDL2_Shutdown();
        ::ImGui::DestroyContext(mine);
        ctx_ = nullptr;
        // Restore the previous current context unless it was the one we just
        // destroyed (DestroyContext already reset it to null).
        if (prev && prev != mine) ::ImGui::SetCurrentContext(prev);
        else ::ImGui::SetCurrentContext(nullptr);
    }
    if (imguiDescriptorPool_ && vkg) {
        vkDestroyDescriptorPool(static_cast<VkDevice>(vkg->getDevice().instance),
                                static_cast<VkDescriptorPool>(imguiDescriptorPool_), nullptr);
        imguiDescriptorPool_ = nullptr;
    }
    if (imguiTexturePool_ && vkg) {
        vkDestroyDescriptorPool(static_cast<VkDevice>(vkg->getDevice().instance),
                                static_cast<VkDescriptorPool>(imguiTexturePool_), nullptr);
        imguiTexturePool_ = nullptr;
    }
    if (imguiTextureLayout_ && vkg) {
        vkDestroyDescriptorSetLayout(static_cast<VkDevice>(vkg->getDevice().instance),
                                     static_cast<VkDescriptorSetLayout>(imguiTextureLayout_),
                                     nullptr);
        imguiTextureLayout_ = nullptr;
    }
#endif
    textures_.clear();
    queuedTextureDraws_.clear();
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
    queuedTextureDraws_.clear();
    if (frameOpen_) {
        ImGui::EndFrame();
        frameOpen_ = false;
    }
#ifdef EVENGINE_WEBGPU
    ImGui_ImplWGPU_NewFrame();
#else
    ImGui_ImplVulkan_NewFrame();
#endif
    ImGui_ImplSDL2_NewFrame(window_);
    ImGui::NewFrame();
    frameOpen_ = true;
}

void ImGuiBackend::applyScale(float scale) {
    if (!initialized_) return;
    scale = std::clamp(scale, 0.5f, 5.f);
    const bool changed = scale != uiScale_;
    uiScale_ = scale;
    setThemeDpiScale(dpiScale_);
    setThemeUiScale(scale);
    applyThemeToImGui(globalTheme(), scale);
    if (changed) rebuildFonts();
}

void ImGuiBackend::setScale(float scale) {
    if (!initialized_) return;
    applyScale(scale);
}

float ImGuiBackend::computeInitialScale() const {
#if defined(EVENGINE_ANDROID) || defined(EVENGINE_IOS)
    float ddpi = 160.f;
    if (SDL_GetDisplayDPI(0, &ddpi, nullptr, nullptr) != 0 || ddpi < 1.f) ddpi = 320.f;
    float s = ddpi / 160.f;
    return std::clamp(s, 1.75f, 3.25f);
#elif defined(_WIN32)
    // A DPI-aware SDL 2.0 window uses physical pixels for both its window and
    // drawable size, so their ratio stays 1 even at 125%/150% Windows scale.
    // Scale ImGui geometry explicitly to retain the OS-requested UI size.
    return computeDpiScale();
#else
    // Desktop: ImGui's backend already applies the display density via
    // io.DisplayFramebufferScale, so the logical (point-space) UI scale stays
    // 1.0. Keeping it constant makes the UI match the OS UI size on
    // high-resolution displays instead of growing with the pixel ratio.
    (void)window_;
    return 1.f;
#endif
}

float ImGuiBackend::computeDpiScale() const {
#if defined(_WIN32)
    const int display = window_ ? SDL_GetWindowDisplayIndex(window_) : 0;
    float ddpi = 96.f;
    if (display >= 0 && SDL_GetDisplayDPI(display, &ddpi, nullptr, nullptr) == 0 && ddpi > 0.f)
        return std::clamp(ddpi / 96.f, 1.f, 4.f);
    return 1.f;
#else
    int logicalW = 0, logicalH = 0, pixelW = 0, pixelH = 0;
    SDL_GetWindowSize(window_, &logicalW, &logicalH);
#ifdef EVENGINE_WEBGPU
    // No GL context on WebGPU; drawable size == window size for the canvas.
    SDL_GetWindowSize(window_, &pixelW, &pixelH);
#else
    SDL_Vulkan_GetDrawableSize(window_, &pixelW, &pixelH);
#endif
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

    // Rasterize at the physical DPI resolution so glyphs stay crisp; the
    // FontGlobalScale set in applyThemeToImGui cancels this so the logical
    // text size stays constant regardless of display density.
    const float sizePx = kBaseFontSizePx * dpiScale_;

    ImFontConfig cfg{};
    cfg.OversampleH = 3;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;
    // A small coverage boost gives Segoe UI's thin strokes enough contrast
    // after the physical-DPI atlas is scaled back into logical coordinates.
    cfg.RasterizerMultiply = 1.12f;
    // Request CJK ranges so the merged CJK font below rasterizes Chinese,
    // Japanese and Korean text (the atlas grows by a few MB — acceptable).
    fontRanges_.clear();
    ImFontGlyphRangesBuilder rangeBuilder;
    rangeBuilder.AddRanges(atlas->GetGlyphRangesDefault());
    rangeBuilder.AddRanges(atlas->GetGlyphRangesChineseFull());
    rangeBuilder.BuildRanges(&fontRanges_);
    cfg.GlyphRanges = fontRanges_.Data;

    bool added = false;
    std::string primaryPath;
    for (const char *path : regularFontCandidates()) {
        if (!fileExists(path)) continue;
        if (atlas->AddFontFromFileTTF(path, sizePx, &cfg)) {
            primaryPath = path;
            added = true;
            break;
        }
    }
    if (!added) atlas->AddFontDefault(&cfg);

    // The first available regular font is usually Latin-only (Segoe UI,
    // DejaVu Sans, Helvetica), so requesting CJK ranges on it adds no Chinese
    // glyphs to the atlas. Merge a CJK-capable system font into the same atlas
    // (same MergeMode pattern as the icon font below) so Chinese/Japanese text
    // renders instead of ImGui's '?' fallback glyph.
    //
    // Glyph coverage cannot be verified until the atlas is rasterized (both
    // ImFont::Glyphs and the IndexLookup table are populated during Build), so
    // pick by file identity: every cjkFontCandidates() entry is CJK-capable,
    // and the first existing one is the best match for the platform. Whether
    // the merge actually covered CJK is checked after upload in
    // checkFontCoverage().
    cjkRanges_.clear();
    ImFontGlyphRangesBuilder cjkRangeBuilder;
    cjkRangeBuilder.AddRanges(atlas->GetGlyphRangesChineseFull());
    cjkRangeBuilder.BuildRanges(&cjkRanges_);
    bool primaryIsCjk = false;
    for (const char *path : cjkFontCandidates()) {
        if (primaryPath == path) {
            primaryIsCjk = true;
            break;
        }
    }
    if (!primaryIsCjk) {
        ImFontConfig cjkCfg{};
        // CJK glyphs are dense (ChineseFull is ~20k+ codepoints); the physical
        // size already scales with DPI, so oversampling 1 keeps the atlas sane.
        cjkCfg.OversampleH = 1;
        cjkCfg.OversampleV = 1;
        cjkCfg.MergeMode = true;
        cjkCfg.GlyphRanges = cjkRanges_.Data;
        for (const char *path : cjkFontCandidates()) {
            if (!fileExists(path)) continue;
            if (atlas->AddFontFromFileTTF(path, sizePx, &cjkCfg, cjkRanges_.Data)) break;
        }
    }

    ImFontConfig iconCfg{};
    iconCfg.OversampleH = 2;
    iconCfg.OversampleV = 2;
    iconCfg.MergeMode = true;
    iconCfg.PixelSnapH = true;
    static const ImWchar iconRanges[] = {0xF000, 0xF8FF, 0};
    for (const std::string &path : iconFontCandidates()) {
        if (!fileExists(path.c_str())) continue;
        if (atlas->AddFontFromFileTTF(path.c_str(), sizePx, &iconCfg, iconRanges)) break;
    }
}

void ImGuiBackend::checkFontCoverage() const {
    // Only valid after the atlas has been built/uploaded (lookup tables exist).
    ImFontAtlas *atlas = ImGui::GetIO().Fonts;
    if (!atlas || atlas->Fonts.Size == 0) return;
    const ImFont *font = atlas->Fonts[0];
    if (font && font->FindGlyphNoFallback(kCjkProbeCodepoint) == nullptr)
        fprintf(stderr,
                "[ui] warning: no CJK-capable system font found; Chinese text will render as '?'\n");
    if (font && font->FindGlyphNoFallback(kIconProbeCodepoint) == nullptr)
        fprintf(stderr,
                "[ui] warning: editor icon font not found; semantic icons will render as '?'\n");
}

void ImGuiBackend::rebuildFonts() {
#ifdef EVENGINE_WEBGPU
    loadFonts();
    ImGui_ImplWGPU_InvalidateDeviceObjects();
    ImGui_ImplWGPU_CreateFontsTexture();
    fontsUploaded_ = true;
#else
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
#endif
}

uint64_t ImGuiBackend::registerTexture(graphics::Texture *tex) {
    if (!initialized_ || !tex || !tex->gpuHandle) return 0;
    RegisteredTexture reg;
    reg.texture = tex;
#ifdef EVENGINE_WEBGPU
    auto *gt = static_cast<eve::graphics::webgpu::GpuTexture *>(tex->gpuHandle);
    reg.imId = (ImTextureID)(intptr_t)gt->view.Get();
    if (!reg.imId) return 0;
#else
    auto *vkg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx_);
    if (!vkg) return 0;
    auto *gt = static_cast<eve::graphics::vulkan::GpuTexture *>(tex->gpuHandle);
    const VkDevice device = static_cast<VkDevice>(vkg->getDevice().instance);
    if (!imguiTextureLayout_) {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = 0;
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = 1;
        info.pBindings = &binding;
        VkDescriptorSetLayout layout = VK_NULL_HANDLE;
        checkVk(vkCreateDescriptorSetLayout(device, &info, nullptr, &layout));
        imguiTextureLayout_ = layout;
    }
    if (!imguiTexturePool_) {
        VkDescriptorPoolSize size{};
        size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        size.descriptorCount = 512;
        VkDescriptorPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        info.maxSets = 512;
        info.poolSizeCount = 1;
        info.pPoolSizes = &size;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        checkVk(vkCreateDescriptorPool(device, &info, nullptr, &pool));
        imguiTexturePool_ = pool;
    }
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = static_cast<VkDescriptorPool>(imguiTexturePool_);
    ai.descriptorSetCount = 1;
    const VkDescriptorSetLayout layout = static_cast<VkDescriptorSetLayout>(imguiTextureLayout_);
    ai.pSetLayouts = &layout;
    checkVk(vkAllocateDescriptorSets(device, &ai, &set));
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = static_cast<VkImageView>(gt->imageView());
    imageInfo.sampler = static_cast<VkSampler>(gt->sampler);
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    reg.imId = set;
#endif
    reg.width = tex->getWidth();
    reg.height = tex->getHeight();
    const uint64_t key = nextTextureKey_++;
    textures_[key] = reg;
    return key;
}

void ImGuiBackend::unregisterTexture(uint64_t id) {
    auto it = textures_.find(id);
    if (it == textures_.end()) return;
#ifndef EVENGINE_WEBGPU
    auto *vkg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx_);
    if (vkg && imguiTexturePool_) {
        VkDescriptorSet set = static_cast<VkDescriptorSet>(it->second.imId);
        vkFreeDescriptorSets(static_cast<VkDevice>(vkg->getDevice().instance),
                             static_cast<VkDescriptorPool>(imguiTexturePool_), 1, &set);
    }
#endif
    textures_.erase(it);
}

bool ImGuiBackend::textureSize(uint64_t id, int *w, int *h) const {
    auto it = textures_.find(id);
    if (it == textures_.end()) return false;
    if (w) *w = it->second.width;
    if (h) *h = it->second.height;
    return true;
}

void *ImGuiBackend::textureHandle(uint64_t id) const {
    auto it = textures_.find(id);
    return it == textures_.end() ? nullptr : static_cast<void *>(it->second.imId);
}

bool ImGuiBackend::usesQueuedTextureDraws() const {
#ifdef EVENGINE_WEBGPU
    return false;
#else
    // The pinned ImGui 1.83 Vulkan renderer always binds its font descriptor
    // and ignores ImDrawCmd::TextureId. EVEngine composites registered textures
    // immediately after ImGui while the same UI render pass is still open.
    return true;
#endif
}

void ImGuiBackend::queueTextureDraw(uint64_t id, float x, float y, float w, float h, float u0,
                                    float v0, float u1, float v1, float r, float g, float b,
                                    float a, bool opaque) {
    if (!usesQueuedTextureDraws() || id == 0 || textures_.find(id) == textures_.end()) return;
    const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
    const ImVec4 clip = ImGui::GetWindowDrawList()->_ClipRectStack.back();
    queuedTextureDraws_.push_back({id, x * scale.x, y * scale.y, w * scale.x, h * scale.y, u0,
                                   v0, u1, v1, r, g, b, a, clip.x * scale.x, clip.y * scale.y,
                                   (clip.z - clip.x) * scale.x, (clip.w - clip.y) * scale.y,
                                   opaque});
}

bool ImGuiBackend::wantCaptureMouse() const {
    if (!initialized_) return false;
    return ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiBackend::wantCaptureKeyboard() const {
    if (!initialized_) return false;
    return ImGui::GetIO().WantCaptureKeyboard;
}

void ImGuiBackend::renderDrawData(void *commandBuffer) {
    if (!initialized_ || !commandBuffer) return;
    ImGui::Render();
    frameOpen_ = false;
#ifdef EVENGINE_WEBGPU
    WGPURenderPassEncoder enc = *static_cast<WGPURenderPassEncoder *>(commandBuffer);
    ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), enc);
#else
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(),
                                    static_cast<VkCommandBuffer>(commandBuffer));
    auto *vkg = dynamic_cast<eve::graphics::vulkan::Graphics *>(gfx_);
    if (vkg && !queuedTextureDraws_.empty()) {
        std::vector<eve::graphics::vulkan::UiTextureDraw> draws;
        draws.reserve(queuedTextureDraws_.size());
        for (const QueuedTextureDraw &queued : queuedTextureDraws_) {
            const auto found = textures_.find(queued.id);
            if (found == textures_.end() || !found->second.texture) continue;
            eve::graphics::vulkan::UiTextureDraw draw;
            draw.texture = found->second.texture;
            draw.x = queued.x;
            draw.y = queued.y;
            draw.w = queued.w;
            draw.h = queued.h;
            draw.u0 = queued.u0;
            draw.v0 = queued.v0;
            draw.u1 = queued.u1;
            draw.v1 = queued.v1;
            draw.tint = eve::graphics::Color(queued.r, queued.g, queued.b, queued.a);
            draw.clipX = queued.clipX;
            draw.clipY = queued.clipY;
            draw.clipW = queued.clipW;
            draw.clipH = queued.clipH;
            draw.opaque = queued.opaque;
            draws.push_back(draw);
        }
        vkg->drawUiTextureRects(commandBuffer, draws);
    }
#endif
}

void ImGuiBackend::presentOverlayThunk(void *userdata, void *commandBuffer) {
    auto *self = static_cast<ImGuiBackend *>(userdata);
    if (!self) return;
    self->renderDrawData(commandBuffer);
}

}  // namespace eve::ui
