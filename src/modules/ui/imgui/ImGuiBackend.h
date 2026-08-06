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
    void applyScale(float scale);

    bool initialized_ = false;
    bool fontsUploaded_ = false;
    bool frameOpen_ = false;
    float uiScale_ = 1.f;
    ImGuiStyle baseStyle_{};
    eve::graphics::Graphics *gfx_ = nullptr;
    SDL_Window *window_ = nullptr;
    void *imguiDescriptorPool_ = nullptr;  // VkDescriptorPool
};

}  // namespace eve::ui
