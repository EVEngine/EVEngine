#include "dialogue/DialogueUX.h"

#include <cctype>
#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::dialogue {

Module_IMPL(DialogueUX, new DialogueUX());

void DialogueUX::record(const std::string& lineId, const std::string& speaker,
                        const std::string& text) {
    history_.push_back({lineId, speaker, plainText(text)});
    if (!lineId.empty()) readLines_.insert(lineId);
}

void DialogueUX::clearHistory() { history_.clear(); }

int DialogueUX::getHistoryCount() const { return static_cast<int>(history_.size()); }

const DialogueUX::HistoryEntry* DialogueUX::historyAt(int index) const {
    return index >= 0 && static_cast<size_t>(index) < history_.size()
               ? &history_[static_cast<size_t>(index)]
               : nullptr;
}

std::string DialogueUX::getHistoryLineId(int index) const {
    const auto* entry = historyAt(index);
    return entry ? entry->lineId : std::string{};
}

std::string DialogueUX::getHistorySpeaker(int index) const {
    const auto* entry = historyAt(index);
    return entry ? entry->speaker : std::string{};
}

std::string DialogueUX::getHistoryText(int index) const {
    const auto* entry = historyAt(index);
    return entry ? entry->text : std::string{};
}

namespace {

bool isControlTag(const std::string& tag) {
    const size_t equal = tag.find('=');
    const std::string name = tag.substr(0, equal);
    return name == "pause" || name == "speed" || name == "color" || name == "/color" ||
           name == "shake" || name == "/shake";
}

}  // namespace

std::string DialogueUX::plainText(const std::string& richText) const {
    std::string out;
    out.reserve(richText.size());
    for (size_t i = 0; i < richText.size();) {
        if (richText[i] == '[') {
            const size_t close = richText.find(']', i + 1);
            if (close != std::string::npos && isControlTag(richText.substr(i + 1, close - i - 1))) {
                i = close + 1;
                continue;
            }
        }
        out.push_back(richText[i++]);
    }
    return out;
}

int DialogueUX::getTextActionCount(const std::string& richText) const {
    int count = 0;
    for (size_t i = 0; i < richText.size();) {
        if (richText[i] == '[') {
            const size_t close = richText.find(']', i + 1);
            if (close != std::string::npos) {
                if (isControlTag(richText.substr(i + 1, close - i - 1))) ++count;
                i = close + 1;
                continue;
            }
        }
        ++i;
    }
    return count;
}

void DialogueUX::setAutoDelay(float seconds) { autoDelay_ = seconds < 0.f ? 0.f : seconds; }

bool DialogueUX::updateAuto(float dt, bool voicePlaying) {
    if (!autoMode_ || voicePlaying) return false;
    autoElapsed_ += dt > 0.f ? dt : 0.f;
    if (autoElapsed_ < autoDelay_) return false;
    autoElapsed_ = 0.f;
    return true;
}

bool DialogueUX::setSkipMode(const std::string& mode) {
    if (mode != "off" && mode != "read" && mode != "all") return false;
    skipMode_ = mode;
    return true;
}

void DialogueUX::markRead(const std::string& lineId) {
    if (!lineId.empty()) readLines_.insert(lineId);
}

bool DialogueUX::isRead(const std::string& lineId) const {
    return !lineId.empty() && readLines_.find(lineId) != readLines_.end();
}

bool DialogueUX::shouldSkip(const std::string& lineId) const {
    return skipMode_ == "all" || (skipMode_ == "read" && isRead(lineId));
}

void DialogueUX::expose(ssq::Table& table) {
    auto cls = table.addClass(name, DialogueUX::create, false);
    expose(cls);
}

void DialogueUX::expose(ssq::Class& cls) {
    cls.addFunc("getName", &DialogueUX::getName);
    cls.addFunc("record", &DialogueUX::record);
    cls.addFunc("clearHistory", &DialogueUX::clearHistory);
    cls.addFunc("getHistoryCount", &DialogueUX::getHistoryCount);
    cls.addFunc("getHistoryLineId", &DialogueUX::getHistoryLineId);
    cls.addFunc("getHistorySpeaker", &DialogueUX::getHistorySpeaker);
    cls.addFunc("getHistoryText", &DialogueUX::getHistoryText);
    cls.addFunc("plainText", &DialogueUX::plainText);
    cls.addFunc("getTextActionCount", &DialogueUX::getTextActionCount);
    cls.addFunc("setAutoMode", &DialogueUX::setAutoMode);
    cls.addFunc("isAutoMode", &DialogueUX::isAutoMode);
    cls.addFunc("setAutoDelay", &DialogueUX::setAutoDelay);
    cls.addFunc("getAutoDelay", &DialogueUX::getAutoDelay);
    cls.addFunc("updateAuto", &DialogueUX::updateAuto);
    cls.addFunc("resetAutoTimer", &DialogueUX::resetAutoTimer);
    cls.addFunc("setSkipMode", &DialogueUX::setSkipMode);
    cls.addFunc("getSkipMode", &DialogueUX::getSkipMode);
    cls.addFunc("markRead", &DialogueUX::markRead);
    cls.addFunc("isRead", &DialogueUX::isRead);
    cls.addFunc("shouldSkip", &DialogueUX::shouldSkip);
}

}  // namespace eve::dialogue
