#pragma once

#include "common/ECS.h"
#include "graphics/DrawItem2D.h"
#include "graphics/Quad.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "zeroerr/assert.h"
#include <cstdint>
#include <vector>

namespace eve::graphics {

class Canvas;

/** Declarative 2D camera (viewport center + zoom). */
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
};

/** Default renderable entity for declarative 2D sprites / solid quads. */
class Renderable2D : public ecs::Entity {
public:
    ENTITY(Renderable2D, ecs::Entity)

    void release() override {}

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
    };

    COMPONENT(Transform2D, transform)
    COMPONENT(Sprite, sprite)
};

class Graphics;

/** Walks ECS Renderable2D views and draws via Graphics batch path. */
class RenderSystem {
public:
    /** Full sprite pass + present (existing tests / sprite-only scenes). */
    static void render(Graphics &gfx);

    /** Append visible Renderable2D sprites into a shared queue. */
    static void collectSprites(std::vector<DrawItem2D> &out);

    /**
     * Sort and draw items. If present=true, calls gfx.present() at the end.
     * Map::render uses present=false so the frame can continue drawing.
     */
    static void drawItems(Graphics &gfx, std::vector<DrawItem2D> &items, bool present);
};

}  // namespace eve::graphics
