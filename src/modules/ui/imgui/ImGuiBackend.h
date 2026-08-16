#pragma once

#include "ui/UIBackend.h"

#include <imgui.h>

namespace eve::ui {

/** Dear ImGui + SDL input + Vulkan present overlay. */
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
    /** Logical (point-space) UI scale; 1.0 on desktop where ImGui handles DPI. */
    float computeInitialScale() const;
    /** Display/framebuffer DPI ratio used to bake the font atlas at native res. */
    float computeDpiScale() const;
    /** Clear the font atlas and re-add fonts at the current physical-pixel size. */
    void loadFonts();
    /** Re-rasterize the font atlas and re-upload its GPU texture (used on scale change). */
    void rebuildFonts();

    bool initialized_ = false;
    bool fontsUploaded_ = false;
    bool frameOpen_ = false;
    float uiScale_ = 1.f;
    float dpiScale_ = 1.f;
    eve::graphics::Graphics *gfx_ = nullptr;
    SDL_Window *window_ = nullptr;
    void *imguiDescriptorPool_ = nullptr;   // VkDescriptorPool
    ImGuiContext *ctx_ = nullptr;           // ImGui context owned by this backend
};

}  // namespace eve::ui
