#pragma once
#include "common/Export.h"


#include <map>
#include <optional>
#include <string>
#include <unordered_map>

namespace eve::graphics {
class Graphics;
class Shader;
}  // namespace eve::graphics

namespace eve::stylize {

class StylePass;

/**
 * @brief Mutable parameter instance of an immutable built-in style definition.
 *
 * Instances hold only user overrides. Shader programs remain owned and shared by
 * Graphics; creating a pass or mesh shader applies this instance's overrides.
 */
class EVENGINE_API_WORLD StyleInstance {
public:
    /** @brief Style instance. */
    explicit StyleInstance(std::string style);
    /** @brief Build an external mesh style from owning resolved scalar defaults; GPU programs are supplied by the
     * package renderer. */
    StyleInstance(std::string style, std::map<std::string, float> defaults);
    /** @brief Style instance. */
    ~StyleInstance() = default;

    StyleInstance(const StyleInstance&)            = delete;
    StyleInstance& operator=(const StyleInstance&) = delete;

    /** @brief Returns the style. */
    std::string getStyle() const { return style_; }
    /** @brief Returns the stage. */
    std::string getStage() const;
    /** @brief Returns the priority. */
    int         getPriority() const;
    /** @brief Override built-in ordering for authored recipe composition. */
    void        setPriority(int priority) { priority_ = priority; }
    /** @brief Restore the style definition's built-in priority. */
    void        resetPriority() { priority_.reset(); }
    /** @brief Requires input. */
    bool        requiresInput(const std::string& input) const;
    /** @brief Returns the param count. */
    int         getParamCount() const;
    /** @brief Returns the param name. */
    std::string getParamName(int index) const;
    /** @brief Returns the param default. */
    float       getParamDefault(const std::string& name) const;
    /** @brief Returns the param min. */
    float       getParamMin(const std::string& name) const;
    /** @brief Returns the param max. */
    float       getParamMax(const std::string& name) const;

    /** @brief True when param. */
    bool  hasParam(const std::string& name) const;
    /** @brief True when overridden. */
    bool  isOverridden(const std::string& name) const;
    /** @brief Sets the float. */
    void  setFloat(const std::string& name, float value);
    /** @brief Returns the float. */
    float getFloat(const std::string& name) const;
    /** @brief Resets . */
    void  reset(const std::string& name);
    /** @brief Resets all. */
    void  resetAll();

    /** @brief Create a post pass and apply this instance's parameter overrides. */
    StylePass* newPass(graphics::Graphics* gfx) const;
    /** @brief Create a mesh technique shader and apply compatible overrides. */
    graphics::Shader* newMeshShader(graphics::Graphics* gfx) const;

    /**
     * @brief Apply current overrides to an existing compatible shader.
     * @param shader Immediate borrowed shader; it is not retained.
     * @thread Render-thread affine when shader is GPU-backed.
     */
    void applyToShader(graphics::Shader* shader) const;

private:
    std::string                            style_;
    std::unordered_map<std::string, float> overrides_;
    std::optional<int>                     priority_;
    std::optional<std::map<std::string, float>> externalDefaults_;
};

}  // namespace eve::stylize
