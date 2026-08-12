#include "animation/SpriteClip.h"

#include "animation/SpriteSheet.h"
#include "common/Exception.h"

#include <cmath>

namespace eve::animation {

SpriteClip::SpriteClip(const std::string &name) : name_(name) {}

void SpriteClip::setName(const std::string &name) { name_ = name; }

void SpriteClip::checkIndex(int index) const {
    if (index < 0 || index >= getFrameCount())
        throw Exception("SpriteClip: frame index %d out of range (count=%d)", index,
                        getFrameCount());
}

void SpriteClip::addFrame(int sheetFrameIndex, float duration) {
    if (sheetFrameIndex < 0)
        throw Exception("SpriteClip.addFrame: sheetFrameIndex must be >= 0");
    if (duration < 0.f) throw Exception("SpriteClip.addFrame: duration must be >= 0");
    frames_.push_back(Entry{sheetFrameIndex, duration});
}

void SpriteClip::addFrameByName(SpriteSheet *sheet, const std::string &frameName,
                                 float duration) {
    if (!sheet) throw Exception("SpriteClip.addFrameByName: sheet is null");
    int idx = sheet->findFrame(frameName);
    if (idx < 0)
        throw Exception("SpriteClip.addFrameByName: unknown frame '%s'", frameName.c_str());
    addFrame(idx, duration);
}

void SpriteClip::clear() { frames_.clear(); }

int SpriteClip::getSheetFrame(int index) const {
    checkIndex(index);
    return frames_[static_cast<size_t>(index)].sheetFrame;
}

float SpriteClip::getFrameDuration(int index) const {
    checkIndex(index);
    return frames_[static_cast<size_t>(index)].duration;
}

float SpriteClip::getDuration() const {
    float sum = 0.f;
    for (const Entry &e : frames_) sum += e.duration;
    return sum;
}

float SpriteClip::wrapTime(float timeSeconds) const {
    if (timeSeconds < 0.f) timeSeconds = 0.f;
    const float dur = getDuration();
    if (dur <= 0.f) return 0.f;
    if (loop_) {
        float t = std::fmod(timeSeconds, dur);
        if (t < 0.f) t += dur;
        return t;
    }
    return timeSeconds > dur ? dur : timeSeconds;
}

int SpriteClip::frameAtTime(float timeSeconds) const {
    if (frames_.empty()) return -1;
    float t = wrapTime(timeSeconds);
    const float dur = getDuration();
    if (!loop_ && timeSeconds >= dur) return getFrameCount() - 1;

    float acc = 0.f;
    for (int i = 0; i < getFrameCount(); ++i) {
        acc += frames_[static_cast<size_t>(i)].duration;
        if (t < acc || i == getFrameCount() - 1) return i;
    }
    return getFrameCount() - 1;
}

}  // namespace eve::animation
