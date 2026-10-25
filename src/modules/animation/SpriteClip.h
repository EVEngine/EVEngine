#pragma once
#include "common/Export.h"


#include <string>
#include <unordered_map>
#include <vector>

namespace eve::animation {

class SpriteSheet;

/**
 * @brief Named 2D frame sequence referencing SpriteSheet frame indices.
 * Script type: `SpriteClip`.
 */
class EVENGINE_API_WORLD SpriteClip {
public:
    /** @brief Sprite clip. */
    explicit SpriteClip(const std::string &name = "");
    /** @brief Sprite clip. */
    ~SpriteClip() = default;

    SpriteClip(const SpriteClip &)            = delete;
    SpriteClip &operator=(const SpriteClip &) = delete;

    /** @brief Sets the name. */
    void        setName(const std::string &name);
    /** @brief Returns the name. */
    std::string getName() const { return name_; }

    /** @brief Sets the loop. */
    void setLoop(bool loop) { loop_ = loop; }
    /** @brief Returns the loop. */
    bool getLoop() const { return loop_; }

    /** @brief Append one cell: sheetFrameIndex + display duration (seconds). */
    void addFrame(int sheetFrameIndex, float duration = 0.1f);

    /** @brief Resolve name via sheet and append. */
    void addFrameByName(SpriteSheet *sheet, const std::string &frameName, float duration = 0.1f);

    /** @brief Append inclusive sheet-frame range at a uniform frame rate. */
    void addRange(int firstSheetFrame, int lastSheetFrame, float fps);
    /** @brief Replace every existing frame duration with 1/fps. */
    void setFPS(float fps);
    /** @brief Uniform FPS, or 0 when empty/variable-duration. */
    float getFPS() const;
    /** @brief Attach a named event to a clip-frame index. */
    void addEvent(int clipFrame, const std::string &name);
    /** @brief Return event name for a frame, or empty when none. */
    std::string getEvent(int clipFrame) const;

    /** @brief Clears . */
    void clear();

    /** @brief Returns the frame count. */
    int   getFrameCount() const { return static_cast<int>(frames_.size()); }
    /** @brief Returns the sheet frame. */
    int   getSheetFrame(int index) const;
    /** @brief Returns the frame duration. */
    float getFrameDuration(int index) const;
    /** @brief Returns the duration. */
    float getDuration() const;

    /**
     * @brief Map absolute time (seconds) into a clip frame index.
     * When loop is false and t >= duration, returns last frame.
     */
    int frameAtTime(float timeSeconds) const;

    /** @brief Local time wrapped or clamped according to loop flag. */
    float wrapTime(float timeSeconds) const;

private:
    struct Entry {
        int   sheetFrame = 0;
        float duration   = 0.1f;
    };

    void checkIndex(int index) const;

    std::string        name_;
    bool               loop_ = true;
    std::vector<Entry> frames_;
    std::unordered_map<int, std::string> events_;
};

}  // namespace eve::animation
