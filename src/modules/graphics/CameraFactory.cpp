#include "common/Assert.h"
#include "graphics/RenderSystem.h"
#include "graphics/RenderSystem3D.h"

namespace eve::graphics {

Camera2D* Camera2D::createCamera(ecs::Table& table) {
    Camera2D* c = Camera2D::create(table);
    ASSERT(c != nullptr);
    c->data()->entity = c;
    c->data()->active = true;
    ASSERT(c->data()->entity == c);
    return c;
}

Camera3D* Camera3D::createCamera(ecs::Table& table) {
    Camera3D* c = Camera3D::create(table);
    ASSERT(c != nullptr);
    c->data()->entity = c;
    c->data()->active = true;
    return c;
}

}  // namespace eve::graphics
