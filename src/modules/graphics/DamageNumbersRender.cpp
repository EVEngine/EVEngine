// eve-hot-path
#include "graphics/DamageNumbersInternal.h"
#include "graphics/Font.h"
#include "graphics/Graphics.h"

namespace eve::graphics {
void DamageNumbers::render(Graphics& graphics, Font& font) const {
    for (const auto& popup : impl_->items) {
        const auto p = Impl::sample(popup);
        if (p.alpha <= 0.f) continue;
        const float x = p.x - font.getWidth(p.text) * p.scale * 0.5f;
        const Color dark(0.025f, 0.02f, 0.04f, p.alpha * 0.9f);
        graphics.drawText(&font, p.text, x + 2.f, p.y + 3.f, dark, p.scale);
        if (p.outline > 0.f) {
            graphics.drawText(&font, p.text, x - p.outline, p.y, dark, p.scale);
            graphics.drawText(&font, p.text, x + p.outline, p.y, dark, p.scale);
            graphics.drawText(&font, p.text, x, p.y - p.outline, dark, p.scale);
            graphics.drawText(&font, p.text, x, p.y + p.outline, dark, p.scale);
        }
        graphics.drawText(&font, p.text, x, p.y, Color(p.r, p.g, p.b, p.alpha), p.scale);
    }
}
}  // namespace eve::graphics
