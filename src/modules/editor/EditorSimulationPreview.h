#pragma once

#include "editor/EditorProtocol.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace eve::editor {

/** @brief Stable transform sample captured from one simulation object. */
struct SimulationObjectSample {
    std::string object;
    double positionX = 0.0, positionY = 0.0, positionZ = 0.0;
    double rotationX = 0.0, rotationY = 0.0, rotationZ = 0.0, rotationW = 1.0;
};

/** @brief One deterministic editor-preview simulation frame. */
struct SimulationPreviewFrame {
    std::uint64_t tick = 0;
    double time = 0.0;
    std::vector<SimulationObjectSample> objects;
};

/** @brief Non-destructive baked preview trajectory and diagnostics. */
struct SimulationBakeResult {
    EditorStatus status = EditorStatus::Failed;
    std::uint64_t sourceRevision = 0;
    double fixedDelta = 0.0;
    std::vector<SimulationPreviewFrame> frames;
    std::vector<EditorDiagnostic> diagnostics;
};

/** @brief Cloneable backend boundary for physics/crowd/fluid editor simulation previews. */
class IEditorSimulationBackend {
public:
    virtual ~IEditorSimulationBackend() = default;
    /** @brief Clone complete simulation state so preview never mutates the live world. */
    virtual std::unique_ptr<IEditorSimulationBackend> cloneForPreview() const = 0;
    /** @brief Advance exactly one fixed deterministic step. */
    virtual EditorResult<void> step(std::uint64_t tick, double fixedDelta) = 0;
    /** @brief Capture stable object transforms after the current step. */
    virtual EditorResult<std::vector<SimulationObjectSample>> capture() const = 0;
};

/** @brief Pause/single-step/bake controller shared by simulation editor modules. */
class SimulationPreviewController {
public:
    /** @brief Set the fixed preview delta in seconds. */
    EditorResult<void> setFixedDelta(double seconds);
    /** @brief Pause or resume interactive preview stepping. */
    void setPaused(bool paused) { paused_ = paused; }
    /** @brief Return whether automatic preview advance is paused. */
    bool paused() const { return paused_; }
    /** @brief Advance one step even while paused, without mutating the live backend. */
    EditorResult<SimulationPreviewFrame> singleStep(const IEditorSimulationBackend& source);
    /** @brief Advance only when unpaused. Paused calls return NoOp. */
    EditorResult<SimulationPreviewFrame> update(const IEditorSimulationBackend& source);
    /** @brief Bake a bounded non-destructive trajectory from a fresh source clone. */
    SimulationBakeResult bake(const IEditorSimulationBackend& source, std::uint64_t sourceRevision,
                              int steps, int maxSteps = 10000) const;
    /** @brief Reset preview clock and discard the isolated preview clone. */
    void rewind();

private:
    EditorResult<SimulationPreviewFrame> advance(IEditorSimulationBackend& backend,
                                                 std::uint64_t tick) const;
    bool paused_ = true;
    double fixedDelta_ = 1.0 / 60.0;
    std::uint64_t tick_ = 0;
    std::unique_ptr<IEditorSimulationBackend> previewBackend_;
};

}  // namespace eve::editor
