#pragma once

#include "common/ECS.h"
#include "zeroerr/assert.h"

#include <cstdint>
#include <string>
#include <glm/glm.hpp>

namespace eve::graphics {

class Canvas;

/** GPU light packing for lit2d (std140-friendly). */
struct Light2DGpu {
    glm::vec4 posRadius{0.f};  // xy = point pos or direction; w = radius (0 => directional)
    glm::vec4 color{0.f};      // rgb * intensity
};

struct Lighting2DUBO {
    static constexpr int kMaxLights = 8;
    glm::vec4 ambient{0.15f, 0.15f, 0.18f, 0.f};
    glm::vec4 meta{0.f};  // x = count, y = viewW, z = viewH
    Light2DGpu lights[kMaxLights]{};
};

/**
 * Declarative 2D light. Collected by RenderSystem (max 8 per canvas/frame).
 * type: "point" | "dir" (≤15 chars).
 */
class Light2D : public ecs::Entity {
public:
    ENTITY(Light2D, ecs::Entity)

    void release() override {}

    struct Data {
        std::string type = "point";
        float x = 0.f;
        float y = 0.f;
        float dx = 0.f;
        float dy = -1.f;
        float r = 1.f, g = 1.f, b = 1.f;
        float intensity = 1.f;
        float radius = 200.f;
        bool enabled = true;
        Canvas *canvas = nullptr;
        Light2D *entity = nullptr;
    };

    COMPONENT(Data, data)

    static Light2D *createLight(const std::string &type = "point");

    void setType(const std::string &type);
    std::string getType();

    void setPosition(float x, float y);
    float getX();
    float getY();

    void setDirection(float dx, float dy);
    float getDirX();
    float getDirY();

    void setColor(float r, float g, float b, float intensity = 1.f);
    void setRadius(float radius);
    float getRadius();

    void setEnabled(bool enabled);
    bool isEnabled();

    void setCanvas(Canvas *canvas);
};

/** GPU light packing for mesh3d / PBR (std140-friendly). */
struct Light3DGpu {
    glm::vec4 posRadius{0.f};  // xyz = point pos or direction; w = radius (0 => directional)
    glm::vec4 color{0.f};      // rgb * intensity
};

struct Lighting3DPack {
    static constexpr int kMaxLights = 8;
    glm::vec4 ambient{0.12f, 0.12f, 0.14f, 0.f};
    Light3DGpu lights[kMaxLights]{};
    int count = 0;
};

/**
 * Declarative 3D light. Collected by RenderSystem3D (max 8 per frame).
 * type: "point" | "dir" (≤15 chars).
 */
class Light3D : public ecs::Entity {
public:
    ENTITY(Light3D, ecs::Entity)

    void release() override {}

    struct Data {
        std::string type = "point";
        float x = 0.f, y = 0.f, z = 0.f;
        float dx = 0.4f, dy = 1.f, dz = 0.3f;
        float r = 1.f, g = 1.f, b = 1.f;
        float intensity = 1.f;
        float radius = 8.f;
        bool enabled = true;
        bool castShadow = false;       // only dir lights; at most one active caster per frame
        float shadowBias = 0.002f;
        float shadowStrength = 1.f;
        Light3D *entity = nullptr;
    };

    COMPONENT(Data, data)

    static Light3D *createLight(const std::string &type = "point");

    void setType(const std::string &type);
    std::string getType();

    void setPosition(float x, float y, float z);
    float getX();
    float getY();
    float getZ();

    void setDirection(float dx, float dy, float dz);
    float getDirX();
    float getDirY();
    float getDirZ();

    void setColor(float r, float g, float b, float intensity = 1.f);
    void setRadius(float radius);
    float getRadius();

    void setEnabled(bool enabled);
    bool isEnabled();

    void setCastShadow(bool cast);
    bool getCastShadow();
    void setShadowBias(float bias);
    float getShadowBias();
    void setShadowStrength(float strength);
    float getShadowStrength();
};

}  // namespace eve::graphics
