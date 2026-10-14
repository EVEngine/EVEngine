#pragma once
#include "common/Export.h"


#include "common/Module.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace eve::dialogue {

/** @brief Presentation state shared by default and custom dialogue UIs. */
class EVENGINE_API_ORCHESTRATION DialogueUX : public Module {
public:
    Module_REG(DialogueUX);

    /** @brief Record a completed line in the player-visible backlog. */
    void record(const std::string& lineId, const std::string& speaker, const std::string& text);
    /** @brief Clears history. */
    void clearHistory();
    /** @brief Returns the history count. */
    int getHistoryCount() const;
    /** @brief Returns the history line id. */
    std::string getHistoryLineId(int index) const;
    /** @brief Returns the history speaker. */
    std::string getHistorySpeaker(int index) const;
    /** @brief Returns the history text. */
    std::string getHistoryText(int index) const;

    /** @brief Strip supported control tags for plain UI widgets. */
    std::string plainText(const std::string& richText) const;
    /** @brief Number of pause/speed/style actions in rich text. */
    int getTextActionCount(const std::string& richText) const;

    /** @brief Sets the auto mode. */
    void setAutoMode(bool enabled) { autoMode_ = enabled; }
    /** @brief True when auto mode. */
    bool isAutoMode() const { return autoMode_; }
    /** @brief Sets the auto delay. */
    void setAutoDelay(float seconds);
    /** @brief Returns the auto delay. */
    float getAutoDelay() const { return autoDelay_; }
    /** @brief Advance the auto timer; voice playback holds the timer. */
    bool updateAuto(float dt, bool voicePlaying);
    /** @brief Resets auto timer. */
    void resetAutoTimer() { autoElapsed_ = 0.f; }

    /** @brief Set skip mode: off, read, or all. */
    bool setSkipMode(const std::string& mode);
    /** @brief Returns the skip mode. */
    std::string getSkipMode() const { return skipMode_; }
    /** @brief Mark read. */
    void markRead(const std::string& lineId);
    /** @brief True when read. */
    bool isRead(const std::string& lineId) const;
    /** @brief Should skip. */
    bool shouldSkip(const std::string& lineId) const;

private:
    struct HistoryEntry {
        std::string lineId;
        std::string speaker;
        std::string text;
    };

    const HistoryEntry* historyAt(int index) const;

    std::vector<HistoryEntry> history_;
    std::unordered_set<std::string> readLines_;
    bool autoMode_ = false;
    float autoDelay_ = 1.5f;
    float autoElapsed_ = 0.f;
    std::string skipMode_ = "off";
};

}  // namespace eve::dialogue
