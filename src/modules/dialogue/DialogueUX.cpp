#include "dialogue/DialogueUX.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <simplesquirrel/simplesquirrel.hpp>
#include <sstream>
#include "audio/Source.h"
#include "dialogue/DialogueVoice.h"

// DialogueUX.cpp
namespace eve::dialogue {

Module_IMPL(DialogueUX, new DialogueUX());

void DialogueUX::record(const std::string& lineId, const std::string& speaker, const std::string& text) {
    history_.push_back({lineId, speaker, plainText(text)});
    if (!lineId.empty()) readLines_.insert(lineId);
}

void DialogueUX::clearHistory() { history_.clear(); }

int DialogueUX::getHistoryCount() const { return static_cast<int>(history_.size()); }

const DialogueUX::HistoryEntry* DialogueUX::historyAt(int index) const {
    return index >= 0 && static_cast<size_t>(index) < history_.size() ? &history_[static_cast<size_t>(index)] : nullptr;
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
    const size_t      equal = tag.find('=');
    const std::string name  = tag.substr(0, equal);
    return name == "pause" || name == "speed" || name == "color" || name == "/color" || name == "shake" ||
           name == "/shake";
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

// DialogueVoice.cpp
namespace eve::dialogue {

Module_IMPL(DialogueVoice, new DialogueVoice());

DialogueVoice::~DialogueVoice() { stop(); }

std::string DialogueVoice::key(const std::string& lineId, const std::string& locale) { return lineId + "\n" + locale; }

bool DialogueVoice::registerClip(const std::string& lineId, const std::string& locale, float duration,
                                 const std::string& envelope) {
    if (lineId.empty() || duration < 0.f) return false;
    Clip& clip    = clips_[key(lineId, locale)];
    clip.lineId   = lineId;
    clip.locale   = locale;
    clip.duration = duration;
    clip.envelope.clear();
    std::istringstream input(envelope);
    std::string        item;
    while (std::getline(input, item, ',')) {
        char*       end   = nullptr;
        const float value = std::strtof(item.c_str(), &end);
        if (end == item.c_str()) return false;
        clip.envelope.push_back(std::clamp(value, 0.f, 1.f));
    }
    return true;
}

bool DialogueVoice::bindSource(const std::string& lineId, const std::string& locale, audio::Source* source) {
    if (!source || lineId.empty()) return false;
    Clip& clip    = clips_[key(lineId, locale)];
    clip.lineId   = lineId;
    clip.locale   = locale;
    clip.source   = source;
    clip.duration = static_cast<float>(source->getDuration());
    return true;
}

DialogueVoice::Clip* DialogueVoice::resolve(const std::string& lineId, const std::string& locale) {
    const auto exact = clips_.find(key(lineId, locale));
    if (exact != clips_.end()) return &exact->second;
    const auto fallback = clips_.find(key(lineId, ""));
    return fallback == clips_.end() ? nullptr : &fallback->second;
}

const DialogueVoice::Clip* DialogueVoice::resolve(const std::string& lineId, const std::string& locale) const {
    return const_cast<DialogueVoice*>(this)->resolve(lineId, locale);
}

bool DialogueVoice::hasClip(const std::string& lineId, const std::string& locale) const {
    return resolve(lineId, locale) != nullptr;
}

void DialogueVoice::clear() {
    stop();
    clips_.clear();
}

bool DialogueVoice::play(const std::string& lineId, const std::string& locale) {
    stop();
    current_ = resolve(lineId, locale);
    if (!current_) return false;
    currentLineId_    = lineId;
    elapsed_          = 0.f;
    simulatedPlaying_ = current_->source == nullptr && current_->duration > 0.f;
    if (current_->source) current_->source->play();
    return true;
}

void DialogueVoice::stop() {
    if (current_ && current_->source) current_->source->stop();
    current_ = nullptr;
    currentLineId_.clear();
    elapsed_          = 0.f;
    simulatedPlaying_ = false;
}

void DialogueVoice::update(float dt) {
    if (!current_ || current_->source) return;
    elapsed_ += std::max(dt, 0.f);
    if (elapsed_ >= current_->duration) simulatedPlaying_ = false;
}

bool DialogueVoice::isPlaying() const {
    return current_ && (current_->source ? current_->source->isPlaying() : simulatedPlaying_);
}

float DialogueVoice::getTime() const {
    return current_ && current_->source ? static_cast<float>(current_->source->tell()) : elapsed_;
}

float DialogueVoice::getDuration() const { return current_ ? current_->duration : 0.f; }

float DialogueVoice::getAmplitude() const {
    if (!current_ || current_->envelope.empty()) return 0.f;
    const size_t index = std::min(static_cast<size_t>(getTime() * envelopeRate_), current_->envelope.size() - 1);
    return current_->envelope[index];
}

bool DialogueVoice::shouldAutoAdvance() const { return current_ && !isPlaying() && getTime() >= current_->duration; }

void DialogueVoice::setEnvelopeRate(float samplesPerSecond) {
    envelopeRate_ = samplesPerSecond > 0.f ? samplesPerSecond : 1.f;
}

void DialogueVoice::expose(ssq::Table& table) {
    auto cls = table.addClass(name, DialogueVoice::create, false);
    expose(cls);
}

void DialogueVoice::expose(ssq::Class& cls) {
    cls.addFunc("getName", &DialogueVoice::getName);
    cls.addFunc("registerClip", &DialogueVoice::registerClip);
    cls.addFunc("bindSource", &DialogueVoice::bindSource);
    cls.addFunc("hasClip", &DialogueVoice::hasClip);
    cls.addFunc("clear", &DialogueVoice::clear);
    cls.addFunc("play", &DialogueVoice::play);
    cls.addFunc("stop", &DialogueVoice::stop);
    cls.addFunc("update", &DialogueVoice::update);
    cls.addFunc("isPlaying", &DialogueVoice::isPlaying);
    cls.addFunc("getTime", &DialogueVoice::getTime);
    cls.addFunc("getDuration", &DialogueVoice::getDuration);
    cls.addFunc("getAmplitude", &DialogueVoice::getAmplitude);
    cls.addFunc("shouldAutoAdvance", &DialogueVoice::shouldAutoAdvance);
    cls.addFunc("getCurrentLineId", &DialogueVoice::getCurrentLineId);
    cls.addFunc("setEnvelopeRate", &DialogueVoice::setEnvelopeRate);
    cls.addFunc("getEnvelopeRate", &DialogueVoice::getEnvelopeRate);
}

}  // namespace eve::dialogue
