#pragma once

#include "common/ECS.h"
#include "graphics/Texture.h"
#include <cstdint>

namespace eve::graphics {

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
    };

    COMPONENT(Transform2D, transform)
    COMPONENT(Sprite, sprite)
};

class Graphics;

/** Walks ECS Renderable2D views and draws via Graphics batch path. */
class RenderSystem {
public:
    static void render(Graphics &gfx);
};

}  // namespace eve::graphics
