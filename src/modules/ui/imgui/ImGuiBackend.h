#pragma once

#include "ui/UIBackend.h"

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

private:
    void renderDrawData(void *vkCommandBuffer);
    static void presentOverlayThunk(void *userdata, void *commandBuffer);

    bool initialized_ = false;
    bool fontsUploaded_ = false;
    eve::graphics::Graphics *gfx_ = nullptr;
    SDL_Window *window_ = nullptr;
    void *imguiDescriptorPool_ = nullptr;  // VkDescriptorPool
};

}  // namespace eve::ui
