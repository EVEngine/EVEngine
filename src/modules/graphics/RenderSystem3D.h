#pragma once

#include "common/ECS.h"
#include "graphics/Mesh.h"
#include "graphics/Shader.h"
#include "graphics/Texture.h"
#include "zeroerr/assert.h"

namespace eve::graphics {

class Graphics;

class Camera3D : public ecs::Entity {
public:
    ENTITY(Camera3D, ecs::Entity)

    void release() override {}

    struct Data {
        float eyeX = 0.f, eyeY = 0.f, eyeZ = 3.f;
        float targetX = 0.f, targetY = 0.f, targetZ = 0.f;
        float upX = 0.f, upY = 1.f, upZ = 0.f;
        float fovYDeg = 60.f, nearZ = 0.1f, farZ = 100.f;
        bool active = false;
        Camera3D *entity = nullptr;
    };

    COMPONENT(Data, data)

    static Camera3D *createCamera() {
        Camera3D *c = Camera3D::create();
        ASSERT(c != nullptr);
        c->data()->entity = c;
        c->data()->active = true;
        return c;
    }

    void setEye(float x, float y, float z);
    void setTarget(float x, float y, float z);
    void setUp(float x, float y, float z);
    void setFov(float fovYDeg);
    void setActive(bool active);
};

class Renderable3D : public ecs::Entity {
public:
    ENTITY(Renderable3D, ecs::Entity)

    void release() override {}

    struct Transform3D {
        float x = 0, y = 0, z = 0;
        float yaw = 0, pitch = 0, roll = 0; // radians
        float sx = 1, sy = 1, sz = 1;
    };

    struct MeshRenderer {
        Mesh *mesh = nullptr;
        Texture *texture = nullptr;
        Shader *shader = nullptr;  // nullptr → default mesh3d lit pipeline
        float r = 1, g = 1, b = 1, a = 1;
        bool visible = true;
        Camera3D *camera = nullptr;
    };

    COMPONENT(Transform3D, transform)
    COMPONENT(MeshRenderer, meshRenderer)

    void setPosition(float x, float y, float z);
    void setRotation(float yaw, float pitch, float roll);
    void setYaw(float yaw);
    float getYaw();
    void setScale(float sx, float sy, float sz);
    void setMesh(Mesh *mesh);
    void setTexture(Texture *texture);
    void setShader(Shader *shader);
    void setTint(float r, float g, float b, float a = 1.f);
    void setVisible(bool visible);
    void setCamera(Camera3D *camera);
};

class RenderSystem3D {
public:
    static void render(Graphics &gfx);
    static void setDirectionalLight(float dx, float dy, float dz, float r, float g, float b);
};

} // namespace eve::graphics
