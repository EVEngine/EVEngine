#pragma once

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
class StyleInstance {
public:
    explicit StyleInstance(std::string style);
    ~StyleInstance() = default;

    StyleInstance(const StyleInstance&)            = delete;
    StyleInstance& operator=(const StyleInstance&) = delete;

    std::string getStyle() const { return style_; }
    std::string getStage() const;
    int         getPriority() const;
    bool        requiresInput(const std::string& input) const;
    int         getParamCount() const;
    std::string getParamName(int index) const;
    float       getParamDefault(const std::string& name) const;
    float       getParamMin(const std::string& name) const;
    float       getParamMax(const std::string& name) const;

    bool  hasParam(const std::string& name) const;
    bool  isOverridden(const std::string& name) const;
    void  setFloat(const std::string& name, float value);
    float getFloat(const std::string& name) const;
    void  reset(const std::string& name);
    void  resetAll();

    /** @brief Create a post pass and apply this instance's parameter overrides. */
    StylePass* newPass(graphics::Graphics* gfx) const;
    /** @brief Create a mesh technique shader and apply compatible overrides. */
    graphics::Shader* newMeshShader(graphics::Graphics* gfx) const;

private:
    void applyOverrides(graphics::Shader* shader) const;

    std::string                            style_;
    std::unordered_map<std::string, float> overrides_;
};

}  // namespace eve::stylize
