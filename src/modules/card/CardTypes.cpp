#include "card/CardTypes.h"

#include "graphics/Graphics.h"

#include <algorithm>
#include <random>

namespace eve::card {

const char *cardStateName(CardState state) {
    switch (state) {
    case CardState::Deck: return "deck";
    case CardState::Hand: return "hand";
    case CardState::Hovered: return "hovered";
    case CardState::Dragging: return "dragging";
    case CardState::Returning: return "returning";
    case CardState::Played: return "played";
    case CardState::Discarded: return "discarded";
    case CardState::Disabled: return "disabled";
    }
    return "unknown";
}

// ---------------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------------

namespace {

struct RenderTransform {
    float cx = 0.f;
    float cy = 0.f;
    float degrees = 0.f;
    bool active = false;
};

RenderTransform gRenderTransform;

glm::vec2 transformedCenter(float x, float y, float w, float h) {
    glm::vec2 center{x + w * 0.5f, y + h * 0.5f};
    if (!gRenderTransform.active || std::abs(gRenderTransform.degrees) < 0.001f) return center;
    const float rad = gRenderTransform.degrees * 3.14159265358979323846f / 180.f;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    const float dx = center.x - gRenderTransform.cx;
    const float dy = center.y - gRenderTransform.cy;
    return {gRenderTransform.cx + dx * c - dy * s, gRenderTransform.cy + dx * s + dy * c};
}

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// 帧率无关的插值系数：k 是 60fps 下的每帧系数，换算到实际 dt。
float frameRateK(float k, float dt) {
    if (dt <= 0.f) return k;
    const float u = 1.f - k;
    const float n = dt * 60.f;
    const int   whole = static_cast<int>(n);
    const float frac = n - static_cast<float>(whole);
    float       r = 1.f;
    for (int i = 0; i < whole; ++i) r *= u;
    r *= (1.f + frac * (u - 1.f));
    return 1.f - r;
}

void drawSolid(graphics::Graphics *gfx, float x, float y, float w, float h, float angle,
               const glm::vec4 &color) {
    if (!gRenderTransform.active || std::abs(angle) < 0.001f)
        gfx->drawSolidRect(x, y, w, h, color);
    else {
        const glm::vec2 center = transformedCenter(x, y, w, h);
        gfx->drawSolidRectRotated(center.x, center.y, w, h, angle, color);
    }
}

void drawTexture(graphics::Graphics *gfx, graphics::Texture *texture, float x, float y, float w, float h,
                 float angle, const glm::vec4 &color) {
    if (!gRenderTransform.active || std::abs(angle) < 0.001f) {
        gfx->drawTexturedRect(texture, x, y, w, h, color);
    } else {
        const glm::vec2 center = transformedCenter(x, y, w, h);
        gfx->drawTexturedRectShaderUVRotated(texture, nullptr, center.x, center.y, w, h,
                                             angle, 0.f, 0.f, 1.f, 1.f, color);
    }
}

void drawGem(graphics::Graphics *gfx, float x, float y, float size, float angle,
             const glm::vec3 &rgb, float a) {
    drawSolid(gfx, x, y, size, size, angle, glm::vec4(0.15f, 0.13f, 0.10f, a));
    const float p = size * 0.20f;
    drawSolid(gfx, x + p, y + p, size - p * 2.f, size - p * 2.f, angle, glm::vec4(rgb, a));
}

}  // namespace

// ---------------------------------------------------------------------------
// CardData
// ---------------------------------------------------------------------------

bool CardData::hit(float px, float py) const {
    if (w <= 0.f || h <= 0.f) return false;
    const float cw = w * scale;
    const float ch = h * scale;
    return px >= x - cw * 0.5f && px <= x + cw * 0.5f && py >= y - ch * 0.5f && py <= y + ch * 0.5f;
}

std::string CardData::describe() const {
    if (kind == "spell") return name + "（法术 · 费用 " + std::to_string(cost) + "）";
    return name + "（" + std::to_string(attack) + "/" + std::to_string(health) + " · 费用 " +
           std::to_string(cost) + "）";
}

// ---------------------------------------------------------------------------
// Deck
// ---------------------------------------------------------------------------

CardData *Deck::draw() {
    if (cards.empty()) return nullptr;
    CardData *top = cards.back();
    cards.pop_back();
    return top;
}

CardData *Deck::peek() const { return cards.empty() ? nullptr : cards.back(); }

CardData *Deck::get(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= cards.size()) return nullptr;
    return cards[static_cast<size_t>(index)];
}

void Deck::shuffle() {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::shuffle(cards.begin(), cards.end(), rng);
}

// ---------------------------------------------------------------------------
// Zone
// ---------------------------------------------------------------------------

bool Zone::contains(float px, float py) const {
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

bool Zone::accepts(const CardData *card) const {
    if (!enabled || !card) return false;
    if (acceptKinds.empty()) return true;
    for (const auto &k : acceptKinds)
        if (k == card->kind) return true;
    return false;
}

void Zone::render(graphics::Graphics *gfx, bool showLabel) {
    gfx->drawSolidRect(x, y, w, h, glm::vec4(color, alpha));
    if (showLabel && gfx->getFont() && !label.empty()) {
        const glm::vec4 tint(clampf(color.r * 0.9f + 0.3f, 0.f, 1.f),
                             clampf(color.g * 0.9f + 0.3f, 0.f, 1.f),
                             clampf(color.b * 0.9f + 0.3f, 0.f, 1.f), 1.f);
        printCentered(gfx, label, x + w * 0.5f, y + h * 0.5f, 0.9f, tint);
    }
}

// ---------------------------------------------------------------------------
// 渲染辅助
// ---------------------------------------------------------------------------

void printCentered(graphics::Graphics *gfx, const std::string &text, float cx, float cy, float scale,
                   const glm::vec4 &color) {
    auto *font = gfx->getFont();
    if (!font) return;
    const float tw = font->getWidth(text) * scale;
    const float th = font->getHeight() * scale;
    // print 的 y 是基线；基线位于字形中心略下方
    gfx->print(text, cx - tw * 0.5f, cy - th * 0.35f, color, scale);
}

void renderCardBack(graphics::Graphics *gfx, float x, float y, float w, float h, float angle, float a) {
    const RenderTransform previous = gRenderTransform;
    gRenderTransform = {x + w * 0.5f, y + h * 0.5f, angle, true};
    drawSolid(gfx, x, y, w, h, angle, glm::vec4(0.06f, 0.06f, 0.10f, a));
    const float i1 = w * 0.05f;
    drawSolid(gfx, x + i1, y + i1, w - i1 * 2.f, h - i1 * 2.f, angle,
              glm::vec4(0.30f, 0.17f, 0.45f, a));
    const float i2 = w * 0.10f;
    drawSolid(gfx, x + i2, y + i2, w - i2 * 2.f, h - i2 * 2.f, angle,
              glm::vec4(0.11f, 0.08f, 0.22f, a));
    if (gfx->getFont())
        printCentered(gfx, "?", x + w * 0.5f, y + h * 0.5f, 1.2f * (w / 110.f),
                      glm::vec4(0.9f, 0.85f, 0.95f, a));
    gRenderTransform = previous;
}

void renderCard(graphics::Graphics *gfx, const CardData &card, const LayoutConfig &cfg, bool back) {
    const float w = card.w * card.scale;
    const float h = card.h * card.scale;
    const float x = card.x - w * 0.5f;
    const float y = card.y - h * 0.5f;
    const float a = card.alpha;

    const RenderTransform previous = gRenderTransform;
    gRenderTransform = {card.x, card.y, card.angle, true};

    if (back || !card.faceUp) {
        renderCardBack(gfx, x, y, w, h, card.angle, a);
        gRenderTransform = previous;
        return;
    }

    // 外框
    drawSolid(gfx, x, y, w, h, card.angle, glm::vec4(0.07f, 0.07f, 0.09f, a));
    const float inset = w * 0.035f;
    const float ix = x + inset;
    const float iy = y + inset;
    const float iw = w - inset * 2.f;
    const float ih = h - inset * 2.f;

    // 卡面底色
    drawSolid(gfx, ix, iy, iw, ih, card.angle, glm::vec4(card.tint, a));

    // 头部色带
    const float headerH = ih * 0.16f;
    drawSolid(gfx, ix, iy, iw, headerH, card.angle,
              glm::vec4(card.tint.r * 0.82f, card.tint.g * 0.82f, card.tint.b * 0.82f, a));

    // 卡图区域
    const float artY = iy + headerH;
    const float artH = ih * 0.34f;
    if (card.texture) {
        drawTexture(gfx, card.texture, ix + 2.f, artY + 2.f, iw - 4.f, artH - 4.f, card.angle,
                    glm::vec4(1.f, 1.f, 1.f, a));
    } else {
        drawSolid(gfx, ix + 2.f, artY + 2.f, iw - 4.f, artH - 4.f, card.angle,
                  glm::vec4(card.tint.r * 0.55f, card.tint.g * 0.55f, card.tint.b * 0.55f, a));
    }

    // 描述文字区
    const float bodyY = artY + artH;
    const float bodyH = iy + ih - bodyY;
    drawSolid(gfx, ix, bodyY, iw, bodyH, card.angle, glm::vec4(0.96f, 0.94f, 0.90f, a));
    drawSolid(gfx, ix + iw * 0.12f, bodyY + bodyH * 0.5f - 1.f, iw * 0.76f, 2.f, card.angle,
              glm::vec4(0.45f, 0.42f, 0.38f, a));

    // 数值宝石
    const float gem = w * 0.13f;
    drawGem(gfx, x + inset, iy, gem, card.angle, glm::vec3(1.00f, 0.78f, 0.25f), a);        // 费用（金）
    if (card.kind != "spell") {
        drawGem(gfx, x + inset, iy + ih - gem, gem, card.angle, glm::vec3(0.92f, 0.38f, 0.22f), a);  // 攻击（橙）
        drawGem(gfx, x + iw - inset - gem, iy + ih - gem, gem, card.angle, glm::vec3(0.30f, 0.78f, 0.42f), a);  // 生命（绿）
    }

    // 文字（可选：需 gfx 已 setFont）
    auto *font = gfx->getFont();
    if (font) {
        const float ts = 1.f * (w / 110.f);
        printCentered(gfx, std::to_string(card.cost), x + inset + gem * 0.5f, iy + gem * 0.55f, ts,
                      glm::vec4(0.12f, 0.08f, 0.05f, a));
        if (card.kind != "spell") {
            printCentered(gfx, std::to_string(card.attack), x + inset + gem * 0.5f, iy + ih - gem * 0.45f,
                          ts, glm::vec4(0.10f, 0.05f, 0.05f, a));
            printCentered(gfx, std::to_string(card.health), x + iw - inset - gem * 0.5f,
                          iy + ih - gem * 0.45f, ts, glm::vec4(0.05f, 0.10f, 0.05f, a));
        }
        printCentered(gfx, card.name, x + w * 0.5f, iy + headerH * 0.5f, ts * 0.8f,
                      glm::vec4(0.10f, 0.08f, 0.06f, a));
    }
    gRenderTransform = previous;
}

// ---------------------------------------------------------------------------
// Hand
// ---------------------------------------------------------------------------

void Hand::addCard(CardData *c) {
    cards.push_back(c);
    if (c) {
        c->state = c->disabled ? CardState::Disabled : CardState::Hand;
        c->w = config ? config->cardW : 110.f;
        c->h = config ? config->cardH : 150.f;
        c->scale = 0.01f;  // 从 0 生长出牌动画
        c->alpha = (c->disabled && config) ? config->disabledAlpha : 1.f;
    }
}

bool Hand::removeCard(CardData *c) {
    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i] == c) {
            cards.erase(cards.begin() + static_cast<ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

void Hand::clear() {
    cards.clear();
    _dragCard = nullptr;
    _pressCard = nullptr;
}

CardData *Hand::get(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= cards.size()) return nullptr;
    return cards[static_cast<size_t>(index)];
}

CardData *Hand::find(const std::string &id) const {
    for (auto *c : cards)
        if (c && c->id == id) return c;
    return nullptr;
}

CardData *Hand::pick(float px, float py) const {
    for (auto it = cards.rbegin(); it != cards.rend(); ++it) {
        if (*it && (*it)->hit(px, py)) return *it;
    }
    return nullptr;
}

void Hand::slotTransform(int i, int n, float &ox, float &oy, float &angle) const {
    const LayoutConfig cfg = config ? *config : LayoutConfig();
    const float totalW = static_cast<float>(n) * cfg.cardW + static_cast<float>(n - 1) * cfg.spacing;
    const float left = cfg.handX - totalW * 0.5f;
    ox = left + static_cast<float>(i) * (cfg.cardW + cfg.spacing) + cfg.cardW * 0.5f;
    const float t = (n > 1) ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.5f;
    const float d = 2.f * t - 1.f;
    oy = cfg.handY - cfg.arcHeight * (d * d);
    angle = cfg.rotationAngle * d;
}

void Hand::update(float dt, float mx, float my, bool down, const std::vector<Zone *> &zones,
                  std::vector<CardEvent> &out) {
    const LayoutConfig cfg = config ? *config : LayoutConfig();
    const int n = static_cast<int>(cards.size());

    // 1) 布局 + 悬浮
    for (int i = 0; i < n; ++i) {
        CardData *card = cards[static_cast<size_t>(i)];
        if (!card || card == _dragCard) continue;

        float sx = 0.f, sy = 0.f, sa = 0.f;
        slotTransform(i, n, sx, sy, sa);

        bool hover = false;
        if (_dragCard == nullptr) hover = (card == pick(mx, my));
        card->hovered = hover;
        if (card->disabled) card->state = CardState::Disabled;
        else if (card->dragging) card->state = CardState::Dragging;
        else if (hover) card->state = CardState::Hovered;
        else if (card->state != CardState::Returning) card->state = CardState::Hand;

        float targetScale = 1.f;
        float targetY = sy;
        float targetAngle = sa;
        if (hover) {
            targetY = sy - cfg.hoverLift;
            targetScale = cfg.hoverScale;
            if (cfg.hoverRotation) targetAngle = 0.f;
        }
        float k = frameRateK(hover ? cfg.hoverSpeed : cfg.motionSpeed, dt);
        card->x = lerpf(card->x, sx, k);
        card->y = lerpf(card->y, targetY, k);
        card->scale = lerpf(card->scale, targetScale, k);
        card->angle = lerpf(card->angle, targetAngle, k);
        card->alpha = lerpf(card->alpha, card->disabled ? cfg.disabledAlpha : 1.f, k);
    }

    // 2) 拖拽状态机
    const bool press = down && !_wasDown;
    const bool release = !down && _wasDown;
    _wasDown = down;

    if (press && interactive) {
        _pressCard = pick(mx, my);
        if (_pressCard && _pressCard->disabled) _pressCard = nullptr;
        _pressX = mx;
        _pressY = my;
    }

    if (down && _pressCard && !_dragCard) {
        const float dx = mx - _pressX;
        const float dy = my - _pressY;
        if (dx * dx + dy * dy > cfg.dragThreshold) {
            _dragCard = _pressCard;
            _dragCard->dragging = true;
            _dragCard->state = CardState::Dragging;
            _dragOx = mx - _dragCard->x;
            _dragOy = my - _dragCard->y;
        }
    }

    if (_dragCard && down) {
        _dragCard->x = mx - _dragOx;
        _dragCard->y = my - _dragOy;
        _dragCard->scale = cfg.hoverScale;
        _dragCard->angle = 0.f;
        _dragCard->alpha = 1.f;
    }

    if (release) {
        if (_dragCard) {
            _dragCard->dragging = false;
            _dragCard->state = CardState::Returning;
            CardData *dropped = _dragCard;
            _dragCard = nullptr;  // 归位；若已从手牌移除则自然消失
            for (Zone *z : zones) {
                if (z && z->contains(mx, my)) {
                    if (z->accepts(dropped)) {
                        out.push_back(CardEvent{"drop", owner, z->id, dropped ? dropped->id : "", ""});
                    } else {
                        out.push_back(CardEvent{"dropRejected", owner, z->id,
                                                dropped ? dropped->id : "", "kind_not_allowed"});
                    }
                    break;
                }
            }
        } else if (_pressCard) {
            out.push_back(CardEvent{"click", owner, "", _pressCard->id, ""});
        }
        _pressCard = nullptr;
    }
}

void Hand::render(graphics::Graphics *gfx) {
    const LayoutConfig cfg = config ? *config : LayoutConfig();
    const bool back = faceDown && !peek;
    const CardData *drag = _dragCard;
    for (auto *c : cards) {
        if (c && c != drag) renderCard(gfx, *c, cfg, back);
    }
    if (drag) renderCard(gfx, *drag, cfg, back);
}

}  // namespace eve::card
