#pragma once

#include "common/Module.h"

#include <string>
#include <unordered_set>
#include <vector>

namespace eve::dialogue {

/** @brief Presentation state shared by default and custom dialogue UIs. */
class DialogueUX : public Module {
public:
    Module_REG(DialogueUX);

    /** @brief Record a completed line in the player-visible backlog. */
    void record(const std::string& lineId, const std::string& speaker, const std::string& text);
    void clearHistory();
    int getHistoryCount() const;
    std::string getHistoryLineId(int index) const;
    std::string getHistorySpeaker(int index) const;
    std::string getHistoryText(int index) const;

    /** @brief Strip supported control tags for plain UI widgets. */
    std::string plainText(const std::string& richText) const;
    /** @brief Number of pause/speed/style actions in rich text. */
    int getTextActionCount(const std::string& richText) const;

    void setAutoMode(bool enabled) { autoMode_ = enabled; }
    bool isAutoMode() const { return autoMode_; }
    void setAutoDelay(float seconds);
    float getAutoDelay() const { return autoDelay_; }
    /** @brief Advance the auto timer; voice playback holds the timer. */
    bool updateAuto(float dt, bool voicePlaying);
    void resetAutoTimer() { autoElapsed_ = 0.f; }

    /** @brief Set skip mode: off, read, or all. */
    bool setSkipMode(const std::string& mode);
    std::string getSkipMode() const { return skipMode_; }
    void markRead(const std::string& lineId);
    bool isRead(const std::string& lineId) const;
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
