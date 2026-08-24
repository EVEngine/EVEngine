#pragma once

#include "ui/UIBackend.h"

#include <imgui.h>

#include <cstdint>
#include <map>
#include <vector>

namespace eve::ui {

/** @brief Dear ImGui + SDL input + Vulkan present overlay. */
class ImGuiBackend final : public UIBackend {
public:
    ImGuiBackend() = default;
    ~ImGuiBackend() override;

    ImGuiBackend(const ImGuiBackend &) = delete;
    ImGuiBackend &operator=(const ImGuiBackend &) = delete;

    bool init(SDL_Window *window, eve::graphics::Graphics *gfx) override;
    void shutdown() override;
    bool isInitialized() const override { return initialized_; }

    void processEvent(const SDL_Event *event) override;
    void newFrame() override;

    bool wantCaptureMouse() const override;
    bool wantCaptureKeyboard() const override;

    void setScale(float scale) override;
    float getScale() const override { return uiScale_; }

private:
    void renderDrawData(void *vkCommandBuffer);
    static void presentOverlayThunk(void *userdata, void *commandBuffer);
    static void windowDestroyedThunk(void *userdata);
    void applyScale(float scale);
    /** @brief Logical (point-space) UI scale; 1.0 on desktop where ImGui handles DPI. */
    float computeInitialScale() const;
    /** @brief Display/framebuffer DPI ratio used to bake the font atlas at native res. */
    float computeDpiScale() const;
    /** @brief Clear the font atlas and re-add fonts at the current physical-pixel size. */
    void loadFonts();
    /** @brief Re-rasterize the font atlas and re-upload its GPU texture (used on scale change). */
    void rebuildFonts();
    /** @brief Warn when the built atlas still lacks CJK glyphs (post-Build only). */
    void checkCjkCoverage() const;

    uint64_t registerTexture(graphics::Texture *tex) override;
    void unregisterTexture(uint64_t id) override;
    bool textureSize(uint64_t id, int *w, int *h) const override;
    void *textureHandle(uint64_t id) const override;
    bool usesQueuedTextureDraws() const override;
    void queueTextureDraw(uint64_t id, float x, float y, float w, float h, float u0, float v0,
                          float u1, float v1, float r, float g, float b, float a,
                          bool opaque) override;

    struct RegisteredTexture {
        ImTextureID imId = nullptr;
        graphics::Texture *texture = nullptr;
        int width = 0;
        int height = 0;
    };
    struct QueuedTextureDraw {
        uint64_t id = 0;
        float x = 0.f;
        float y = 0.f;
        float w = 0.f;
        float h = 0.f;
        float u0 = 0.f;
        float v0 = 0.f;
        float u1 = 1.f;
        float v1 = 1.f;
        float r = 1.f;
        float g = 1.f;
        float b = 1.f;
        float a = 1.f;
        float clipX = 0.f;
        float clipY = 0.f;
        float clipW = 0.f;
        float clipH = 0.f;
        bool opaque = false;
    };
    std::map<uint64_t, RegisteredTexture> textures_;
    std::vector<QueuedTextureDraw> queuedTextureDraws_;
    uint64_t nextTextureKey_ = 1;
    ImVector<ImWchar> fontRanges_;  // kept alive for cfg.GlyphRanges across font builds
    ImVector<ImWchar> cjkRanges_;   // kept alive for the merged CJK font config

    bool initialized_ = false;
    bool fontsUploaded_ = false;
    bool frameOpen_ = false;
    float uiScale_ = 1.f;
    float dpiScale_ = 1.f;
    eve::graphics::Graphics *gfx_ = nullptr;
    SDL_Window *window_ = nullptr;
    void *imguiDescriptorPool_ = nullptr;   // VkDescriptorPool
    void *imguiTexturePool_ = nullptr;      // VkDescriptorPool (texture sets)
    void *imguiTextureLayout_ = nullptr;    // VkDescriptorSetLayout (texture sets)
    ImGuiContext *ctx_ = nullptr;           // ImGui context owned by this backend
};

}  // namespace eve::ui
