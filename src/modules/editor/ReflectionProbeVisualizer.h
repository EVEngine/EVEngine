#pragma once

#include <string>

namespace eve::graphics {
class ReflectionProbeCapture;
}

namespace eve::editor {

/** @brief Renderer-independent wire-box and status description for a reflection probe. */
class ReflectionProbeVisualizer {
public:
    /** @brief Construct a visualizer attached to a runtime reflection probe. */
    explicit ReflectionProbeVisualizer(graphics::ReflectionProbeCapture *probe);
    /** @brief Set positive influence-box half extents. */
    void setExtents(float x, float y, float z);
    /** @brief Return the number of wire-box line segments. */
    int getLineCount() const { return 12; }
    /** @brief Return one line start coordinate, where component is 0=x,1=y,2=z. */
    float getLineStart(int line, int component) const;
    /** @brief Return one line end coordinate, where component is 0=x,1=y,2=z. */
    float getLineEnd(int line, int component) const;
    /** @brief Return status color red channel. */
    float getColorR() const;
    /** @brief Return status color green channel. */
    float getColorG() const;
    /** @brief Return status color blue channel. */
    float getColorB() const;
    /** @brief Return probe center X. */
    float getCenterX() const;
    /** @brief Return probe center Y. */
    float getCenterY() const;
    /** @brief Return probe center Z. */
    float getCenterZ() const;
    /** @brief Return a concise capture/staging/publication status annotation. */
    std::string getStatusLabel() const;

private:
    float coordinate(int corner, int component) const;

    graphics::ReflectionProbeCapture *probe_ = nullptr;
};

}  // namespace eve::editor
