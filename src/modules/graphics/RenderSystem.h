#pragma once

#include "common/ECS.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Quad.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "zeroerr/assert.h"
#include <cstdint>
#include <string>
#include <vector>

namespace eve::graphics {

class Canvas;

/** @brief Declarative 2D camera (viewport center + zoom). */
class Camera2D : public ecs::Entity {
public:
    ENTITY(Camera2D, ecs::Entity)

    void release() override {}

    struct Data {
        float x = 0.f;
        float y = 0.f;
        float zoom = 1.f;
        float r = 0.1f, g = 0.1f, b = 0.12f, a = 1.f;
        float ambientR = 0.15f, ambientG = 0.15f, ambientB = 0.18f;
        bool active = false; // true only after createCamera() / explicit enable
        Canvas *canvas = nullptr;
        Camera2D *entity = nullptr;  // self; set by createCamera()
    };

    COMPONENT(Data, data)

    static Camera2D *createCamera() {
        Camera2D *c = Camera2D::create();
        ASSERT(c != nullptr);
        c->data()->entity = c;
        c->data()->active = true;
        ASSERT(c->data()->entity == c);
        return c;
    }

    void setAmbient(float r, float g, float b);

    /** @brief World-space look-at center and zoom (1 = identity). */
    void  setPosition(float x, float y);
    float getX();
    float getY();
    void  setZoom(float zoom);
    float getZoom();

    /**
     * @brief Convert screen pixel (origin top-left of viewport) to world coordinates.
     * viewW/viewH are the current drawable size (e.g. gfx.getWidth/Height).
     */
    float screenToWorldX(float screenX, float screenY, float viewW, float viewH);
    float screenToWorldY(float screenX, float screenY, float viewW, float viewH);
    float worldToScreenX(float worldX, float worldY, float viewW, float viewH);
    float worldToScreenY(float worldX, float worldY, float viewW, float viewH);
};

/** @brief Default renderable entity for declarative 2D sprites / solid quads. */
class Renderable2D : public ecs::Entity {
public:
    ENTITY(Renderable2D, ecs::Entity)

    void release() override;

    struct Transform2D {
        float x = 0;
        float y = 0;
        float rot = 0;
        float sx = 1;
        float sy = 1;
    };

    struct Sprite {
        float width = 32;
        float height = 32;
        float r = 1, g = 1, b = 1, a = 1;
        int layer = 0;
        bool visible = true;
        Texture *texture = nullptr;
        Texture *normalTexture = nullptr;  // non-null → GPU lit2d path
        Quad *quad = nullptr;              // nullptr → full UV 0..1
        Shader *shader = nullptr;          // nullptr → default textured / solid pipeline
        Canvas *canvas = nullptr;          // nullptr → screen
        Camera2D *camera = nullptr;        // nullptr → default active camera for canvas
        bool receiveLight = true;          // false → force unlit (ignore lights)
        bool castOcclusion = true;         // volumetric occlusion map (shadow analogue)
        BlendMode blend = BlendMode::Alpha;
        float anchorX = 0.5f, anchorY = 0.5f;
        bool flipX = false, flipY = false;
        int trimW = 0, trimH = 0, offsetX = 0, offsetY = 0;
    };

    COMPONENT(Transform2D, transform)
    COMPONENT(Sprite, sprite)

    /** @brief Set world-space position in pixels. */
    void setPosition(float x, float y);
    /** @brief Return world-space X position. */
    float getX();
    /** @brief Return world-space Y position. */
    float getY();
    /** @brief Set center rotation in degrees. */
    void setRotation(float degrees);
    /** @brief Return center rotation in degrees. */
    float getRotation();
    /** @brief Set independent X/Y scale. */
    void setScale(float sx, float sy);
    /** @brief Return X scale. */
    float getScaleX();
    /** @brief Return Y scale. */
    float getScaleY();
    /** @brief Set unscaled sprite dimensions in pixels. */
    void setSize(float width, float height);
    /** @brief Return unscaled width. */
    float getWidth();
    /** @brief Return unscaled height. */
    float getHeight();
    /** @brief Assign the borrowed sprite texture. */
    void setTexture(Texture *texture);
    /** @brief Return the assigned texture. */
    Texture *getTexture();
    /** @brief Assign the borrowed UV quad. */
    void setQuad(Quad *quad);
    /** @brief Return the assigned UV quad. */
    Quad *getQuad();
    /** @brief Set RGBA tint. */
    void setColor(float r, float g, float b, float a = 1.f);
    /** @brief Set integer painter-order layer. */
    void setLayer(int layer);
    /** @brief Return painter-order layer. */
    int getLayer();
    /** @brief Include or exclude the sprite from collection. */
    void setVisible(bool visible);
    /** @brief Return whether the sprite is visible. */
    bool getVisible();
    /** @brief Enable or disable 2D light reception. */
    void setReceiveLight(bool receive);
    /** @brief Return whether the sprite receives 2D lights. */
    bool getReceiveLight();
    /** @brief Set blend mode (`alpha` or `additive`). */
    void setBlend(const std::string &blend);
    /** @brief Return blend mode name. */
    std::string getBlend();
    /** @brief Set normalized transform pivot; (0,0) top-left, (0.5,0.5) center. */
    void setAnchor(float x, float y);
    /** @brief Return normalized horizontal pivot. */
    float getAnchorX();
    /** @brief Return normalized vertical pivot. */
    float getAnchorY();
    /** @brief Mirror atlas UVs without changing transform scale. */
    void setFlip(bool horizontal, bool vertical);
    /** @brief Return horizontal mirror state. */
    bool getFlipX();
    /** @brief Return vertical mirror state. */
    bool getFlipY();
    /** @brief Apply trimmed-frame layout while preserving the original canvas origin. */
    void setFrameLayout(int sourceW, int sourceH, int trimW, int trimH, int offsetX, int offsetY);
    /** @brief Enable or disable volumetric occlusion casting. */
    void setCastOcclusion(bool cast);
    /** @brief Return whether the sprite casts volumetric occlusion. */
    bool getCastOcclusion();
};

class Graphics;

/** @brief Walks ECS Renderable2D views and draws via Graphics batch path. */
class RenderSystem {
public:
    /** @brief Full sprite pass + present (existing tests / sprite-only scenes). */
    static void render(Graphics &gfx);

    /** @brief Append visible Renderable2D sprites into a shared queue. */
    static void collectSprites(std::vector<DrawItem2D> &out);

    /**
     * @brief Sort and draw items. If present=true, calls gfx.present() at the end.
     * Map::render uses present=false so the frame can continue drawing.
     */
    static void drawItems(Graphics &gfx, std::vector<DrawItem2D> &items, bool present);
};

}  // namespace eve::graphics
