#include "graphics/RenderSystem.h"
#include "graphics/Graphics.h"
#include "graphics/Canvas.h"
#include "zeroerr/assert.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace eve::graphics {

namespace {

float clampZoom(float z) { return z <= 0.f ? 1e-4f : z; }

struct ViewCam {
    float x = 0.f;
    float y = 0.f;
    float zoom = 1.f;
    bool valid = false;
    Color clearColor{0.1f, 0.1f, 0.12f, 1.f};
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

}  // namespace

void RenderSystem::render(Graphics &gfx) {
    std::unordered_map<Canvas *, Camera2D *> defaultCam;
    // View() calls ensure_space(registy) and crashes if no Camera2D was ever created.
    if (ecs::ComponentManager<Camera2D>::inst().registy != nullptr) {
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

    struct Item {
        float x, y, w, h;
        Color color;
        int layer;
        Texture *texture;
        Shader *shader;
        Canvas *canvas;
        ViewCam cam;
    };
    std::vector<Item> items;

    auto view = ecs::View<Renderable2D, Renderable2D::Transform2D, Renderable2D::Sprite>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [xf, sp] = *it;
        if (!sp->visible) continue;

        Camera2D *camEnt = sp->camera;
        if (!camEnt) {
            auto found = defaultCam.find(sp->canvas);
            camEnt = (found != defaultCam.end()) ? found->second : nullptr;
        }

        Item item;
        item.x = xf->x;
        item.y = xf->y;
        item.w = sp->width * xf->sx;
        item.h = sp->height * xf->sy;
        item.color = Color(sp->r, sp->g, sp->b, sp->a);
        item.layer = sp->layer;
        item.texture = sp->texture;
        item.shader = sp->shader;
        item.canvas = sp->canvas;
        item.cam = fromEntity(camEnt);
        items.push_back(item);
    }

    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        const bool aOff = a.canvas != nullptr;
        const bool bOff = b.canvas != nullptr;
        if (aOff != bOff) return aOff && !bOff;
        if (a.canvas != b.canvas) return a.canvas < b.canvas;
        if (a.layer != b.layer) return a.layer < b.layer;
        if (a.shader != b.shader) return a.shader < b.shader;
        return a.texture < b.texture;
    });

    // Per canvas group: setCanvas → clear → draw. No trailing empty screen group
    // when the frame only touched offscreen targets.
    Canvas *current = reinterpret_cast<Canvas *>(static_cast<uintptr_t>(1));
    for (size_t i = 0; i < items.size(); ++i) {
        Canvas *next = items[i].canvas;
        if (i == 0 || next != current) {
            Color clearCol = gfx.getBackgroundColor();
            auto defIt = defaultCam.find(next);
            if (defIt != defaultCam.end()) clearCol = fromEntity(defIt->second).clearColor;

            gfx.setCanvas(next);
            gfx.clear(clearCol, std::nullopt, std::nullopt);
            current = next;
        }

        const auto &it = items[i];
        int viewW = it.canvas ? it.canvas->getWidth() : gfx.getWidth();
        int viewH = it.canvas ? it.canvas->getHeight() : gfx.getHeight();
        float sx, sy, sw, sh;
        applyCamera(it.x, it.y, it.w, it.h, it.cam, viewW, viewH, sx, sy, sw, sh);
        if (it.texture)
            gfx.drawTexturedRectShader(it.texture, it.shader, sx, sy, sw, sh, it.color);
        else
            gfx.drawSolidRect(sx, sy, sw, sh, it.color);
    }

    gfx.setCanvas();
    gfx.present();
}

}  // namespace eve::graphics
