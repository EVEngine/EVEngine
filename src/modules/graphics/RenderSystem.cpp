#include "graphics/RenderSystem.h"
#include "graphics/Graphics.h"
#include "graphics/Canvas.h"
#include "graphics/Light.h"
#include "graphics/Quad.h"
#include "common/Exception.h"
#include "common/RenderTrace.h"
#include "zeroerr/assert.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace eve::graphics {

void Renderable2D::release() { ecs::DestroyEntity(this); }

void Renderable2D::setPosition(float x, float y) {
    transform()->x = x;
    transform()->y = y;
}
float Renderable2D::getX() { return transform()->x; }
float Renderable2D::getY() { return transform()->y; }
void Renderable2D::setRotation(float degrees) { transform()->rot = degrees; }
float Renderable2D::getRotation() { return transform()->rot; }
void Renderable2D::setScale(float sx, float sy) {
    transform()->sx = sx;
    transform()->sy = sy;
}
float Renderable2D::getScaleX() { return transform()->sx; }
float Renderable2D::getScaleY() { return transform()->sy; }
void Renderable2D::setSize(float width, float height) {
    if (width < 0.f || height < 0.f)
        throw Exception("Sprite2D.setSize: width/height must be >= 0");
    sprite()->width = width;
    sprite()->height = height;
}
float Renderable2D::getWidth() { return sprite()->width; }
float Renderable2D::getHeight() { return sprite()->height; }
void Renderable2D::setTexture(Texture *texture) { sprite()->texture = texture; }
Texture *Renderable2D::getTexture() { return sprite()->texture; }
void Renderable2D::setQuad(Quad *quad) { sprite()->quad = quad; }
Quad *Renderable2D::getQuad() { return sprite()->quad; }
void Renderable2D::setColor(float r, float g, float b, float a) {
    sprite()->r = r;
    sprite()->g = g;
    sprite()->b = b;
    sprite()->a = a;
}
void Renderable2D::setLayer(int layer) { sprite()->layer = layer; }
int Renderable2D::getLayer() { return sprite()->layer; }
void Renderable2D::setVisible(bool visible) { sprite()->visible = visible; }
bool Renderable2D::getVisible() { return sprite()->visible; }
void Renderable2D::setReceiveLight(bool receive) { sprite()->receiveLight = receive; }
bool Renderable2D::getReceiveLight() { return sprite()->receiveLight; }
void Renderable2D::setBlend(const std::string &blend) {
    if (blend == "alpha") sprite()->blend = BlendMode::Alpha;
    else if (blend == "additive" || blend == "add") sprite()->blend = BlendMode::Additive;
    else if (blend == "opaque") sprite()->blend = BlendMode::Opaque;
    else if (blend == "premultiplied" || blend == "premultiplied_alpha")
        sprite()->blend = BlendMode::Premultiplied;
    else if (blend == "multiply") sprite()->blend = BlendMode::Multiply;
    else throw Exception(
        "Sprite2D.setBlend: expected alpha|additive|opaque|premultiplied|multiply");
}
std::string Renderable2D::getBlend() {
    switch (sprite()->blend) {
        case BlendMode::Additive: return "additive";
        case BlendMode::Opaque: return "opaque";
        case BlendMode::Premultiplied: return "premultiplied";
        case BlendMode::Multiply: return "multiply";
        default: return "alpha";
    }
}
void Renderable2D::setAnchor(float x, float y) {
    sprite()->anchorX = x;
    sprite()->anchorY = y;
}
float Renderable2D::getAnchorX() { return sprite()->anchorX; }
float Renderable2D::getAnchorY() { return sprite()->anchorY; }
void Renderable2D::setFlip(bool horizontal, bool vertical) {
    sprite()->flipX = horizontal;
    sprite()->flipY = vertical;
}
bool Renderable2D::getFlipX() { return sprite()->flipX; }
bool Renderable2D::getFlipY() { return sprite()->flipY; }
void Renderable2D::setFrameLayout(int sourceW, int sourceH, int trimW, int trimH, int offsetX, int offsetY) {
    setSize(float(sourceW), float(sourceH));
    sprite()->trimW = trimW;
    sprite()->trimH = trimH;
    sprite()->offsetX = offsetX;
    sprite()->offsetY = offsetY;
}

void Camera2D::setAmbient(float r, float g, float b) {
    auto d = data();
    d->ambientR = r;
    d->ambientG = g;
    d->ambientB = b;
}

void Camera2D::setPosition(float x, float y) {
    auto d = data();
    d->x = x;
    d->y = y;
}

float Camera2D::getX() { return data()->x; }
float Camera2D::getY() { return data()->y; }

void Camera2D::setZoom(float zoom) { data()->zoom = zoom; }
float Camera2D::getZoom() { return data()->zoom; }

float Camera2D::screenToWorldX(float screenX, float /*screenY*/, float viewW, float /*viewH*/) {
    auto d = data();
    float z = d->zoom <= 0.f ? 1e-4f : d->zoom;
    return (screenX - viewW * 0.5f) / z + d->x;
}

float Camera2D::screenToWorldY(float /*screenX*/, float screenY, float /*viewW*/, float viewH) {
    auto d = data();
    float z = d->zoom <= 0.f ? 1e-4f : d->zoom;
    return (screenY - viewH * 0.5f) / z + d->y;
}

float Camera2D::worldToScreenX(float worldX, float /*worldY*/, float viewW, float /*viewH*/) {
    auto d = data();
    float z = d->zoom <= 0.f ? 1e-4f : d->zoom;
    return (worldX - d->x) * z + viewW * 0.5f;
}

float Camera2D::worldToScreenY(float /*worldX*/, float worldY, float /*viewW*/, float viewH) {
    auto d = data();
    float z = d->zoom <= 0.f ? 1e-4f : d->zoom;
    return (worldY - d->y) * z + viewH * 0.5f;
}

void Renderable2D::setCastOcclusion(bool cast) { sprite()->castOcclusion = cast; }

bool Renderable2D::getCastOcclusion() { return sprite()->castOcclusion; }

namespace {

float clampZoom(float z) { return z <= 0.f ? 1e-4f : z; }

struct ViewCam {
    float x = 0.f;
    float y = 0.f;
    float zoom = 1.f;
    bool valid = false;
    Color clearColor{0.1f, 0.1f, 0.12f, 1.f};
    float ambientR = 0.15f, ambientG = 0.15f, ambientB = 0.18f;
};

ViewCam fromEntity(Camera2D *ent) {
    ViewCam v;
    if (!ent) return v;
    auto d = ent->data();
    v.valid = true;
    v.x = d->x;
    v.y = d->y;
    v.zoom = d->zoom;
    v.clearColor = Color(d->r, d->g, d->b, d->a);
    v.ambientR = d->ambientR;
    v.ambientG = d->ambientG;
    v.ambientB = d->ambientB;
    return v;
}

void applyCamera(float wx, float wy, float ww, float wh, const ViewCam &cam, int viewW, int viewH,
                 float &sx, float &sy, float &sw, float &sh) {
    if (!cam.valid) {
        sx = wx;
        sy = wy;
        sw = ww;
        sh = wh;
        return;
    }
    ASSERT_GT(viewW, 0);
    ASSERT_GT(viewH, 0);
    const float z = clampZoom(cam.zoom);
    ASSERT_GT(z, 0.f);
    sx = (wx - cam.x) * z + float(viewW) * 0.5f;
    sy = (wy - cam.y) * z + float(viewH) * 0.5f;
    sw = ww * z;
    sh = wh * z;
}

struct PackedLight {
    Light2D::Data *data = nullptr;
    bool isPoint = true;
};

void collectLights(Canvas *canvasKey, std::vector<PackedLight> &out) {
    out.clear();
    if (ecs::current()->getManager<Light2D>() == nullptr) return;
    auto view = ecs::View<Light2D, Light2D::Data>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [d] = *it;
        if (!d->enabled) continue;
        if (d->canvas != canvasKey) continue;
        PackedLight pl;
        pl.data = d;
        pl.isPoint = (d->type != "dir");
        out.push_back(pl);
    }
    std::stable_sort(out.begin(), out.end(), [](const PackedLight &a, const PackedLight &b) {
        if (a.isPoint != b.isPoint) return a.isPoint && !b.isPoint;
        return a.data->intensity > b.data->intensity;
    });
    if (out.size() > size_t(Lighting2DUBO::kMaxLights))
        out.resize(size_t(Lighting2DUBO::kMaxLights));
}

Lighting2DUBO packLights(const std::vector<PackedLight> &lights, const ViewCam &cam, int viewW,
                         int viewH) {
    Lighting2DUBO ubo{};
    ubo.ambient = glm::vec4(cam.ambientR, cam.ambientG, cam.ambientB, 0.f);
    ubo.meta = glm::vec4(float(lights.size()), float(viewW), float(viewH), 0.f);
    for (size_t i = 0; i < lights.size(); ++i) {
        const auto *d = lights[i].data;
        Light2DGpu &g = ubo.lights[i];
        g.color = glm::vec4(d->r * d->intensity, d->g * d->intensity, d->b * d->intensity, 1.f);
        if (lights[i].isPoint) {
            float lx = d->x, ly = d->y;
            float lr = d->radius;
            if (cam.valid) {
                const float z = clampZoom(cam.zoom);
                lx = (d->x - cam.x) * z + float(viewW) * 0.5f;
                ly = (d->y - cam.y) * z + float(viewH) * 0.5f;
                lr = d->radius * z;
            }
            g.posRadius = glm::vec4(lx, ly, 0.f, lr);
        } else {
            float dx = d->dx, dy = d->dy;
            const float len = std::sqrt(dx * dx + dy * dy);
            if (len > 1e-6f) {
                dx /= len;
                dy /= len;
            } else {
                dx = 0.f;
                dy = -1.f;
            }
            g.posRadius = glm::vec4(dx, dy, 0.f, 0.f);
        }
    }
    return ubo;
}

Color modulateUnlit(Color base, float cx, float cy, bool receiveLight, const Lighting2DUBO &ubo) {
    if (!receiveLight) return base;
    glm::vec3 lit(ubo.ambient.r, ubo.ambient.g, ubo.ambient.b);
    const int count = int(ubo.meta.x + 0.5f);
    for (int i = 0; i < count; ++i) {
        const auto &L = ubo.lights[i];
        glm::vec3 col(L.color.r, L.color.g, L.color.b);
        if (L.posRadius.w <= 0.f) {
            // Directional: approximate as constant contribution for unlit sprites.
            lit += col * 0.65f;
        } else {
            const float dx = L.posRadius.x - cx;
            const float dy = L.posRadius.y - cy;
            const float dist = std::sqrt(dx * dx + dy * dy);
            float atten = 1.f - dist / std::max(L.posRadius.w, 1.f);
            if (atten < 0.f) atten = 0.f;
            atten *= atten;
            lit += col * atten;
        }
    }
    return Color(base.r * lit.r, base.g * lit.g, base.b * lit.b, base.a);
}

}  // namespace

void RenderSystem::collectSprites(std::vector<DrawItem2D> &out) {
    std::unordered_map<Canvas *, Camera2D *> defaultCam;
    if (ecs::current()->getManager<Camera2D>() != nullptr) {
        auto camView = ecs::View<Camera2D, Camera2D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data] = *it;
            if (!data->active) continue;
            ASSERT(data->entity != nullptr);
            if (!data->entity) continue;
            Canvas *key = data->canvas;
            if (defaultCam.find(key) == defaultCam.end()) defaultCam[key] = data->entity;
        }
    }

    auto view = ecs::View<Renderable2D, Renderable2D::Transform2D, Renderable2D::Sprite>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [xf, sp] = *it;
        if (!sp->visible) continue;

        Camera2D *camEnt = sp->camera;
        if (!camEnt) {
            auto found = defaultCam.find(sp->canvas);
            camEnt = (found != defaultCam.end()) ? found->second : nullptr;
        }

        ViewCam cam = fromEntity(camEnt);
        DrawItem2D item;
        item.x = xf->x + float(sp->offsetX) * xf->sx;
        item.y = xf->y + float(sp->offsetY) * xf->sy;
        item.w = float(sp->trimW > 0 ? sp->trimW : sp->width) * xf->sx;
        item.h = float(sp->trimH > 0 ? sp->trimH : sp->height) * xf->sy;
        item.depthY = xf->y + sp->height * xf->sy;
        item.rotation = xf->rot;
        item.anchorX = sp->trimW > 0
                           ? (sp->anchorX * sp->width - float(sp->offsetX)) / float(sp->trimW)
                           : sp->anchorX;
        item.anchorY = sp->trimH > 0
                           ? (sp->anchorY * sp->height - float(sp->offsetY)) / float(sp->trimH)
                           : sp->anchorY;
        item.flipX = sp->flipX;
        item.flipY = sp->flipY;
        item.color = Color(sp->r, sp->g, sp->b, sp->a);
        item.layer = sp->layer;
        item.blend = sp->blend;
        item.texture = sp->texture;
        item.normal = sp->normalTexture;
        item.quad = sp->quad;
        item.shader = sp->shader;
        item.canvas = sp->canvas;
        item.camera = camEnt;
        item.camValid = cam.valid;
        item.camX = cam.x;
        item.camY = cam.y;
        item.camZoom = cam.zoom;
        item.camClear = cam.clearColor;
        item.camAmbientR = cam.ambientR;
        item.camAmbientG = cam.ambientG;
        item.camAmbientB = cam.ambientB;
        item.receiveLight = sp->receiveLight;
        item.litPath = sp->receiveLight && sp->normalTexture != nullptr && sp->texture != nullptr &&
                       sp->shader == nullptr;
        if (item.quad && item.texture) {
            item.hasUV = true;
            item.quad->getUV(item.texture->getWidth(), item.texture->getHeight(), item.u0, item.v0,
                             item.u1, item.v1);
        }
        out.push_back(item);
    }
}

void RenderSystem::drawItems(Graphics &gfx, std::vector<DrawItem2D> &items, bool present) {
    eve::debug::RenderPassScope pass("RenderSystem2D");
    sortDrawItems2D(items);

    std::unordered_map<Canvas *, Camera2D *> defaultCam;
    if (ecs::current()->getManager<Camera2D>() != nullptr) {
        auto camView = ecs::View<Camera2D, Camera2D::Data>();
        for (auto it = camView.begin(); it != camView.end(); ++it) {
            auto [data] = *it;
            if (!data->active) continue;
            if (!data->entity) continue;
            Canvas *key = data->canvas;
            if (defaultCam.find(key) == defaultCam.end()) defaultCam[key] = data->entity;
        }
    }

    Canvas *current = reinterpret_cast<Canvas *>(static_cast<uintptr_t>(1));
    Lighting2DUBO currentLights{};
    for (size_t i = 0; i < items.size(); ++i) {
        Canvas *next = items[i].canvas;
        if (i == 0 || next != current) {
            Color clearCol = gfx.getBackgroundColor();
            ViewCam groupCam{};
            auto defIt = defaultCam.find(next);
            if (defIt != defaultCam.end()) {
                groupCam = fromEntity(defIt->second);
                clearCol = groupCam.clearColor;
            } else if (items[i].camValid) {
                groupCam.valid = true;
                groupCam.x = items[i].camX;
                groupCam.y = items[i].camY;
                groupCam.zoom = items[i].camZoom;
                groupCam.clearColor = items[i].camClear;
                groupCam.ambientR = items[i].camAmbientR;
                groupCam.ambientG = items[i].camAmbientG;
                groupCam.ambientB = items[i].camAmbientB;
                clearCol = groupCam.clearColor;
            }

            gfx.setCanvas(next);
            eve::debug::rtTarget(next ? "canvas" : "screen");
            // Only clear when presenting a full sprite pass; map-unified draws must not wipe.
            if (present) gfx.clear(clearCol, std::nullopt, std::nullopt);
            current = next;

            int viewW = next ? next->getWidth() : gfx.getWidth();
            int viewH = next ? next->getHeight() : gfx.getHeight();
            std::vector<PackedLight> packed;
            collectLights(next, packed);
            currentLights = packLights(packed, groupCam, viewW, viewH);
            gfx.setLighting2D(currentLights);
        }

        const auto &it = items[i];
        ViewCam cam{};
        if (it.camValid) {
            cam.valid = true;
            cam.x = it.camX;
            cam.y = it.camY;
            cam.zoom = it.camZoom;
        } else if (it.camera) {
            cam = fromEntity(it.camera);
        }
        int viewW = it.canvas ? it.canvas->getWidth() : gfx.getWidth();
        int viewH = it.canvas ? it.canvas->getHeight() : gfx.getHeight();
        float sx, sy, sw, sh;
        applyCamera(it.x, it.y, it.w, it.h, cam, viewW, viewH, sx, sy, sw, sh);

        float u0 = it.hasUV ? it.u0 : 0.f;
        float v0 = it.hasUV ? it.v0 : 0.f;
        float u1 = it.hasUV ? it.u1 : 1.f;
        float v1 = it.hasUV ? it.v1 : 1.f;
        if (!it.hasUV && it.quad && it.texture)
            it.quad->getUV(it.texture->getWidth(), it.texture->getHeight(), u0, v0, u1, v1);
        if (it.flipX) std::swap(u0, u1);
        if (it.flipY) std::swap(v0, v1);

        const float pivotX = sx + sw * it.anchorX;
        const float pivotY = sy + sh * it.anchorY;
        const float radians = it.rotation * 3.14159265358979323846f / 180.f;
        const float ox = sw * (0.5f - it.anchorX);
        const float oy = sh * (0.5f - it.anchorY);
        const float centerX = pivotX + ox * std::cos(radians) - oy * std::sin(radians);
        const float centerY = pivotY + ox * std::sin(radians) + oy * std::cos(radians);

        if (it.sceneColorDistortion) {
            eve::debug::rtBind("texture", "distortion");
            eve::debug::rtBind("texture", "sceneColor");
            eve::debug::rtDraw("tryDrawSceneColorDistortionUVRotated", "distortion");
            gfx.tryDrawSceneColorDistortionUVRotated(it.texture, centerX, centerY, sw, sh, it.rotation, u0, v0, u1, v1,
                                                     it.distortionStrength, it.color.a, it.rotatedUV);
        } else if (it.litPath) {
            eve::debug::rtBind("texture", "albedo");
            eve::debug::rtBind("texture", "normal");
            eve::debug::rtDraw("drawTexturedRectLitUV", "lit2d");
            gfx.drawTexturedRectLitUV(it.texture, it.normal, sx, sy, sw, sh, u0, v0, u1, v1,
                                      it.color);
        } else if (it.texture) {
            Color c = it.color;
            if (it.receiveLight && !it.normal)
                c = modulateUnlit(c, sx + sw * 0.5f, sy + sh * 0.5f, true, currentLights);
            eve::debug::rtBind("texture", "sprite");
            if (it.shader) eve::debug::rtBind("shader", "custom");
            if (it.rotation != 0.f) {
                eve::debug::rtDraw("drawTexturedRectShaderUVRotated",
                                   it.shader ? "shader" : "textured");
                gfx.drawTexturedRectShaderUVRotated(it.texture, it.shader, centerX,
                                                    centerY, sw, sh, it.rotation, u0, v0, u1,
                                                    v1, c, it.rotatedUV, it.blend);
            } else {
                eve::debug::rtDraw("drawTexturedRectShaderUV", it.shader ? "shader" : "textured");
                gfx.drawTexturedRectShaderUV(it.texture, it.shader, sx, sy, sw, sh, u0, v0, u1, v1, c,
                                             it.rotatedUV, it.blend);
            }
        } else {
            Color c = it.color;
            if (it.receiveLight)
                c = modulateUnlit(c, sx + sw * 0.5f, sy + sh * 0.5f, true, currentLights);
            eve::debug::rtDraw("drawSolidRect", "solid");
            if (it.rotation != 0.f)
                gfx.drawSolidRectRotated(centerX, centerY, sw, sh, it.rotation, c,
                                         it.blend);
            else
                gfx.drawSolidRect(sx, sy, sw, sh, c, it.blend);
        }
    }

    if (present) {
        gfx.setCanvas();
        eve::debug::rtPassBegin("Present");
        gfx.present();
        eve::debug::rtPassEnd("Present");
    }
}

void RenderSystem::render(Graphics &gfx) {
    eve::debug::rtFrameBegin();
    std::vector<DrawItem2D> items;
    collectSprites(items);
    drawItems(gfx, items, true);
    eve::debug::rtFrameEnd();
}

}  // namespace eve::graphics
