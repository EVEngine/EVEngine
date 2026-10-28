#pragma once
#include "common/Export.h"


#include "common/Result.h"

#include <string>
#include <vector>

namespace ssq { class Table; }

namespace eve::ui {

/** @brief One missing Pcg terrain scene and the objects retaining its regular or impostor representation. */
struct PcgMissingTerrainScene {
    std::string terrainName;
    std::string impostorName;
    std::vector<std::string> regularReferences;
    std::vector<std::string> impostorReferences;
};

/**
 * @brief Caller-owned loading-screen state driven by Pcg terrain load-progress events.
 * @details The terrain loader forwards start, update, end and timeout events. This object owns diagnostic strings,
 *          invokes no callbacks and exposes an immutable UI snapshot through getters.
 * @thread UI thread only.
 */
class EVENGINE_API_WORLD PcgLoadingScreen {
public:
    /** @brief Set the alpha units removed per second. */
    [[nodiscard]] Result<void> configure(float fadeOutSpeed);
    /** @brief Reset canvas, background alpha and progress when load tracking starts. */
    void onLoadProgressStarted() noexcept;
    /** @brief Publish progress; values at least one automatically begin fade-out. */
    [[nodiscard]] Result<void> onLoadProgressUpdated(float progress);
    /** @brief Hide progress/text and begin fading the background. */
    void onLoadProgressEnded() noexcept;
    /** @brief Start a transactional missing-scene diagnostic. */
    void beginTimeout() noexcept;
    /** @brief Append one missing terrain scene and return its stable index in the pending diagnostic. */
    [[nodiscard]] Result<int> addMissingScene(const std::string& terrainName, const std::string& impostorName);
    /** @brief Append a regular referencing object name to a pending scene. */
    [[nodiscard]] Result<void> addRegularReference(int sceneIndex, const std::string& objectName);
    /** @brief Append an impostor referencing object name to a pending scene. */
    [[nodiscard]] Result<void> addImpostorReference(int sceneIndex, const std::string& objectName);
    /** @brief Publish the timeout warning and begin fade-out. */
    [[nodiscard]] Result<void> endTimeout();
    /** @brief Advance background fade using explicit frame delta time. */
    [[nodiscard]] Result<void> tick(float deltaTime);

    /** @brief Return whether the loading canvas is enabled. */
    bool getCanvasVisible() const noexcept { return canvasVisible_; }
    /** @brief Return whether the progress bar is active. */
    bool getProgressVisible() const noexcept { return progressVisible_; }
    /** @brief Return whether the loading text is enabled. */
    bool getTextVisible() const noexcept { return textVisible_; }
    /** @brief Return whether fade-out is running. */
    bool getFading() const noexcept { return fading_; }
    /** @brief Return current progress without clamping Pcg's event value. */
    float getProgress() const noexcept { return progress_; }
    /** @brief Return current background alpha. */
    float getBackgroundAlpha() const noexcept { return backgroundAlpha_; }
    /** @brief Return the most recently published timeout warning. */
    const std::string& getTimeoutMessage() const noexcept { return timeoutMessage_; }

private:
    [[nodiscard]] Result<void> addReference(int sceneIndex, const std::string& objectName, bool impostor);

    float fadeOutSpeed_ = 0.f;
    float progress_ = 0.f;
    float backgroundAlpha_ = 1.f;
    bool canvasVisible_ = false;
    bool progressVisible_ = true;
    bool textVisible_ = true;
    bool fading_ = false;
    bool timeoutOpen_ = false;
    std::vector<PcgMissingTerrainScene> pendingMissingScenes_;
    std::string timeoutMessage_;
};

/** @brief Register Pcg loading-screen bindings. */
void exposePcgLoadingScreenBindings(ssq::Table& table);

}  // namespace eve::ui
