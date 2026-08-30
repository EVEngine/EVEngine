#pragma once

#include "common/Module.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::audio {
class Source;
}

namespace eve::dialogue {

/** @brief Localized voice assets, duration-based advance, and amplitude lip sync. */
class DialogueVoice : public Module {
public:
    Module_REG(DialogueVoice);
    ~DialogueVoice() override;

    /** @brief Register clip metadata; envelope is comma-separated 0..1 amplitudes. */
    bool registerClip(const std::string& lineId, const std::string& locale, float duration,
                      const std::string& envelope);
    /** @brief Bind a playable Audio Source; the caller retains source ownership. */
    bool bindSource(const std::string& lineId, const std::string& locale, audio::Source* source);
    bool hasClip(const std::string& lineId, const std::string& locale) const;
    void clear();

    bool play(const std::string& lineId, const std::string& locale);
    void stop();
    void update(float dt);
    bool isPlaying() const;
    float getTime() const;
    float getDuration() const;
    float getAmplitude() const;
    bool shouldAutoAdvance() const;
    std::string getCurrentLineId() const { return currentLineId_; }
    void setEnvelopeRate(float samplesPerSecond);
    float getEnvelopeRate() const { return envelopeRate_; }

private:
    struct Clip {
        std::string lineId;
        std::string locale;
        float duration = 0.f;
        std::vector<float> envelope;
        audio::Source* source = nullptr;
    };

    static std::string key(const std::string& lineId, const std::string& locale);
    Clip* resolve(const std::string& lineId, const std::string& locale);
    const Clip* resolve(const std::string& lineId, const std::string& locale) const;

    std::unordered_map<std::string, Clip> clips_;
    Clip* current_ = nullptr;
    std::string currentLineId_;
    float elapsed_ = 0.f;
    float envelopeRate_ = 30.f;
    bool simulatedPlaying_ = false;
};

}  // namespace eve::dialogue
