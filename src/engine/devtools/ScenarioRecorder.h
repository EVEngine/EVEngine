#pragma once

#include "common/Export.h"

#include <functional>
#include <string>
#include <vector>

struct SQVM;
typedef struct SQVM* HSQUIRRELVM;

namespace eve::platform_event {
class PlatformEvent;
class Message;
}  // namespace eve::platform_event

namespace eve::dev {

/** @brief One serializable event payload recorded from the event queue. */
struct EVENGINE_API ScenarioEvent {
    std::string              name;  /**< @brief Event/message name ("keypressed", ...). */
    std::vector<std::string> args;  /**< @brief String payloads (non-string payloads dropped). */
};

/** @brief All events consumed during one game frame. */
struct EVENGINE_API ScenarioFrame {
    int                          frame = 0;  /**< @brief Frame index within the scenario. */
    std::vector<ScenarioEvent>   events;     /**< @brief Events consumed that frame, in order. */
};

/**
 * @brief A state-driven bug scenario: baseline snapshot + ordered per-frame inputs.
 *
 * The engine treats itself as stateless (recoverable game state lives in script
 * variables), so restoring the baseline snapshot and re-injecting the recorded
 * per-frame input/event stream reproduces the original run deterministically
 * (given deterministic game logic and RNG seeds).
 *
 * Record: begin() captures the baseline, an observer on the event module records
 * every consumed message per frame (markFrame()), end() persists the scenario to
 * a JSON file alongside the error report.
 *
 * Replay: beginReplay() restores the baseline, then each stageFrame() re-pushes
 * one recorded frame's events into the event queue; the caller advances one game
 * frame between calls and watches for the error to reproduce.
 */
class EVENGINE_API ScenarioRecorder {
public:
    static ScenarioRecorder& instance();

    ScenarioRecorder(const ScenarioRecorder&)            = delete;
    ScenarioRecorder& operator=(const ScenarioRecorder&) = delete;

    // ---- recording -----------------------------------------------------
    /**
     * @brief Start recording: capture the baseline snapshot and observe the event queue.
     * @return false when the VM is null or the baseline capture fails.
     */
    bool begin(HSQUIRRELVM vm, std::string* err = nullptr);
    /** @brief Begin a new frame bucket; no-op unless recording. */
    void markFrame();
    /**
     * @brief Stop recording and persist the scenario to a JSON file.
     * @param path Destination file (e.g. "scenario_boss.json").
     * @return false when not recording or the file cannot be written.
     */
    bool end(const std::string& path, std::string* err = nullptr);
    /** @brief Abort recording and restore the previous event observer. */
    void cancel();
    /** @brief True while a scenario is being recorded. */
    bool recording() const { return vm_ != nullptr && recording_; }

    // ---- replay ---------------------------------------------------------
    /**
     * @brief Load a scenario and restore its baseline snapshot onto the VM.
     * @return false when the file is invalid or restore fails.
     */
    bool beginReplay(HSQUIRRELVM vm, const std::string& path, std::string* err = nullptr);
    /**
     * @brief Re-push the next recorded frame's events into the event queue.
     * @return false when replay is exhausted (all frames staged) or not replaying.
     */
    bool stageFrame();
    /** @brief True while a replay is in progress with frames remaining. */
    bool replaying() const { return replay_; }
    /** @brief Number of frames remaining to stage. */
    int  framesRemaining() const { return replay_ ? static_cast<int>(frames_.size()) - replayIndex_ : 0; }

    // ---- DevTool wiring --------------------------------------------------
    /** @brief Called by DevTool when a script error is dumped during recording. */
    void setErrorInfo(const std::string& report, const std::string& site);
    /** @brief Invoked by the event poll observer for every consumed message. */
    void onEventConsumed(const eve::platform_event::Message& msg);
    /** @brief Ensure the event observer is detached (restore previous); safe to call repeatedly. */
    void detachObserver();

    // ---- accessors --------------------------------------------------------
    const std::string& baseline() const { return baseline_; }
    const std::vector<ScenarioFrame>& frames() const { return frames_; }
    const std::string& errorReport() const { return errorReport_; }
    const std::string& errorSite() const { return errorSite_; }

    /** @brief Persist a scenario to a JSON file (used for both record and replay paths). */
    static bool save(const std::string& baseline, const std::vector<ScenarioFrame>& frames,
                     const std::string& errorReport, const std::string& errorSite,
                     const std::string& path, std::string* err = nullptr);
    /** @brief Load a scenario from a JSON file into the given outputs. */
    static bool load(const std::string& path, std::string* baseline,
                     std::vector<ScenarioFrame>* frames, std::string* errorReport,
                     std::string* errorSite, std::string* err = nullptr);

private:
    ScenarioRecorder() = default;

    void setObserver();

    HSQUIRRELVM                            vm_ = nullptr;
    bool                                   recording_ = false;
    bool                                   replay_ = false;
    int                                    replayIndex_ = 0;
    int                                    currentFrame_ = 0;
    std::string                            baseline_;
    std::vector<ScenarioFrame>             frames_;
    std::string                            errorReport_;
    std::string                            errorSite_;
    std::function<void(const eve::platform_event::Message&)> savedObserver_;
    eve::platform_event::PlatformEvent*                     eventModule_ = nullptr;
};

}  // namespace eve::dev
