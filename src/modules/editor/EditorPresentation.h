#pragma once

#include <string>

namespace eve::editor {

/** @brief A point in the coordinate space chosen by an editor viewport adapter. */
struct OverlayPoint {
    float x = 0.f;
    float y = 0.f;
    float z = 0.f;
};

/** @brief Renderer-independent appearance for an editor overlay primitive. */
struct OverlayStyle {
    unsigned int color = 0xffffffffU;
    float thickness = 1.f;
    bool filled = false;
};

/**
 * @brief Sink implemented by a 2D or 3D viewport to render tool feedback.
 *
 * Tools emit semantic geometry through this interface and never depend on a
 * particular renderer, scene graph or immediate-mode UI library.
 */
class IEditorOverlay {
public:
    virtual ~IEditorOverlay() = default;

    /** @brief Draw a line between two viewport-space points. */
    virtual void line(const OverlayPoint &from, const OverlayPoint &to, const OverlayStyle &style) = 0;
    /** @brief Draw a circle whose orientation is supplied by the viewport. */
    virtual void circle(const OverlayPoint &center, float radius, const OverlayStyle &style) = 0;
    /** @brief Draw an axis-aligned rectangle in viewport coordinates. */
    virtual void rectangle(const OverlayPoint &minimum, const OverlayPoint &maximum, const OverlayStyle &style) = 0;
    /** @brief Draw a short text annotation. */
    virtual void text(const OverlayPoint &position, const std::string &value, const OverlayStyle &style) = 0;
};

/**
 * @brief UI-independent property editor used by tools to expose settings.
 *
 * Each function returns true when the host changed the referenced value.
 */
class IEditorInspector {
public:
    virtual ~IEditorInspector() = default;

    /** @brief Begin a logical group of properties. */
    virtual void beginGroup(const std::string &id, const std::string &label) = 0;
    /** @brief End the current logical group. */
    virtual void endGroup() = 0;
    /** @brief Present an editable boolean property. */
    virtual bool boolean(const std::string &id, const std::string &label, bool &value) = 0;
    /** @brief Present an editable integer property. */
    virtual bool integer(const std::string &id, const std::string &label, int &value, int minimum, int maximum) = 0;
    /** @brief Present an editable scalar property. */
    virtual bool scalar(const std::string &id, const std::string &label, float &value, float minimum, float maximum) = 0;
    /** @brief Present an editable text property. */
    virtual bool string(const std::string &id, const std::string &label, std::string &value) = 0;
};

}  // namespace eve::editor
