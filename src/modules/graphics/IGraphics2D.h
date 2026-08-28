#pragma once

// Narrow 2D-drawing interface of the Graphics backend.
//
// Consumers that only draw 2D (map tiles, particles, UI, camera previews)
// depend on this interface instead of the full graphics::Graphics god class:
// changing the 2D surface no longer ripples into 3D consumers and vice versa.
// The concrete backend class implements all facets; methods here are pure
// virtual so no backend can silently skip a draw.

#include "graphics/BlendMode.h"
#include "graphics/Color.h"

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace eve::graphics {

class Canvas;
class Camera2D;
class Drawable;
class Font;
class Shader;
class Texture;
struct Lighting2DUBO;

/** @brief 2D immediate-mode drawing surface (screen or offscreen Canvas). */
class IGraphics2D {
public:
    virtual ~IGraphics2D() = default;

    /** @brief Clears the current draw target to the background color. */
    virtual void clearScreen() = 0;
    virtual void setBackgroundColorRGBA(float r, float g, float b, float a = 1.f) = 0;
    virtual void drawSolidRectRGBA(float x, float y, float w, float h, float r, float g, float b,
                                   float a = 1.f) = 0;
    virtual void drawTexturedRectRGBA(Texture *texture, float x, float y, float w, float h,
                                      float r, float g, float b, float a = 1.f) = 0;

    virtual void drawSolidRect(float x, float y, float w, float h, const Color &color,
                               BlendMode blend = BlendMode::Alpha) = 0;
    virtual void drawSolidRectRotated(float cx, float cy, float w, float h, float degrees,
                                      const Color &color,
                                      BlendMode blend = BlendMode::Alpha) = 0;

    virtual void drawTexturedRect(Texture *texture, float x, float y, float w, float h,
                                  const Color &color) = 0;
    virtual void drawTexturedRectShader(Texture *texture, Shader *shader, float x, float y, float w,
                                        float h, const Color &color) = 0;
    virtual void drawTexturedRectUV(Texture *texture, float x, float y, float w, float h, float u0,
                                    float v0, float u1, float v1, const Color &color) = 0;
    virtual void drawTexturedRectShaderUV(Texture *texture, Shader *shader, float x, float y,
                                          float w, float h, float u0, float v0, float u1, float v1,
                                          const Color &color, bool rotatedUV = false,
                                          BlendMode blend = BlendMode::Alpha) = 0;
    virtual void drawTexturedRectShaderUVRotated(Texture *texture, Shader *shader, float cx,
                                                 float cy, float w, float h, float degrees,
                                                 float u0, float v0, float u1, float v1,
                                                 const Color &color, bool rotatedUV = false,
                                                 BlendMode blend = BlendMode::Alpha) = 0;
    virtual void drawTexturedRectShaderDepth(Texture *color, Texture *depth, Shader *shader,
                                             float x, float y, float w, float h,
                                             const Color &tint) = 0;

    /** @brief Lit 2D draw (albedo + normal map); normal may be null → flat. */
    virtual void drawTexturedRectLitUV(Texture *albedo, Texture *normal, float x, float y, float w,
                                       float h, float u0, float v0, float u1, float v1,
                                       const Color &color) = 0;
    /** @brief Upload per-frame / per-canvas 2D lighting constants. */
    virtual void setLighting2D(const Lighting2DUBO &ubo) = 0;

    /** @brief Draw target; nullptr → screen. */
    virtual void setCanvas(Canvas *canvas) = 0;
    virtual bool isCanvasActive() const = 0;
    virtual Canvas *getCanvas() const = 0;

    /** @brief Custom fragment pipeline for subsequent textured 2D draws. */
    virtual void setShader(Shader *shader) = 0;
    virtual void setShader() = 0;
    virtual Shader *getShader() const = 0;

    /** @brief Optional shared font used by legacy consumers; nullptr = none set. */
    virtual void setFont(Font *font) = 0;
    virtual Font *getFont() const = 0;
    /**
     * @brief Draws UTF-8 text with the font selected by setFont().
     * @param text Borrowed UTF-8 text retained only for this call.
     * @param x Left edge in the current canvas coordinate space.
     * @param y Top edge in the current canvas coordinate space.
     * @param color Glyph tint and opacity.
     * @param scale Uniform text scale.
     * @throws eve::Exception if no current font has been selected.
     * @note Render-thread only. The call is synchronous and invokes no callbacks.
     */
    virtual void print(const std::string &text, float x, float y,
                       const Color &color = Color(1.f, 1.f, 1.f, 1.f), float scale = 1.f) = 0;
    /**
     * @brief Draws UTF-8 text with an explicitly supplied GPU font.
     * @param font Borrowed non-null font belonging to this graphics backend; it
     * is not retained beyond the call.
     * @param text Borrowed UTF-8 text retained only for this call.
     * @param x Left edge in the current canvas coordinate space.
     * @param y Top edge in the current canvas coordinate space.
     * @param color Glyph tint and opacity.
     * @param scale Uniform text scale.
     * @throws eve::Exception if `font` is nullptr.
     * @note Render-thread only. The call is synchronous and invokes no callbacks.
     */
    virtual void drawText(Font *font, const std::string &text, float x, float y,
                          const Color &color = Color(1.f, 1.f, 1.f, 1.f), float scale = 1.f) = 0;

    virtual void draw(Drawable *drawable, const glm::mat4 &m) = 0;
    virtual void drawOcclusion(Drawable *drawable, const glm::mat4 &m) = 0;
    virtual void drawOcclusionSolid(float x, float y, float w, float h) = 0;
    virtual void drawOcclusionTexture(Texture *texture, float x, float y, float w, float h) = 0;

    /** @brief Backend GPU validation scopes (WebGPU). */
    virtual void pushValidationScope() = 0;
    virtual void popValidationScope() = 0;
};

}  // namespace eve::graphics
