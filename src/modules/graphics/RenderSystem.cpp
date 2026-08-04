#include "graphics/RenderSystem.h"
#include "graphics/Graphics.h"
#include <vector>
#include <algorithm>

namespace eve::graphics {

void RenderSystem::render(Graphics &gfx) {
    struct Item {
        float x, y, w, h;
        Color color;
        int layer;
        Texture *texture;
    };
    std::vector<Item> items;

    auto view = ecs::View<Renderable2D, Renderable2D::Transform2D, Renderable2D::Sprite>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [xf, sp] = *it;
        if (!sp->visible) continue;
        Item item;
        item.x = xf->x;
        item.y = xf->y;
        item.w = sp->width * xf->sx;
        item.h = sp->height * xf->sy;
        item.color = Color(sp->r, sp->g, sp->b, sp->a);
        item.layer = sp->layer;
        item.texture = sp->texture;
        items.push_back(item);
    }

    // Layer first; then texture pointer to keep same-texture draws consecutive for batching.
    std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
        if (a.layer != b.layer) return a.layer < b.layer;
        return a.texture < b.texture;
    });

    gfx.clear(gfx.getBackgroundColor(), std::nullopt, std::nullopt);
    for (const auto &it : items) {
        if (it.texture)
            gfx.drawTexturedRect(it.texture, it.x, it.y, it.w, it.h, it.color);
        else
            gfx.drawSolidRect(it.x, it.y, it.w, it.h, it.color);
    }
    gfx.present();
}

}  // namespace eve::graphics
