#include "card/CardTypes.h"

#include "card/CardAttributes.h"

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

CardData *CardData::createCard() {
    CardData *c = CardData::create();
    c->identity();
    c->stats();
    c->attributes();
    c->effects();
    c->visual();
    c->layout();
    c->state();
    c->definitionBinding();
    return c;
}

bool CardData::hit(float px, float py) {
    const auto *L = layout().operator->();
    if (L->w <= 0.f || L->h <= 0.f) return false;
    const float cw = L->w * L->scale;
    const float ch = L->h * L->scale;
    return px >= L->x - cw * 0.5f && px <= L->x + cw * 0.5f && py >= L->y - ch * 0.5f &&
           py <= L->y + ch * 0.5f;
}

std::string CardData::describe() {
    const auto *I = identity().operator->();
    auto projected = CardAttributeAdapter::project(*this);
    if (!projected) projected.ignore("card description retained legacy projection after attribute failure");
    const auto *S = stats().operator->();
    if (I->kind == "spell") return I->name + "（法术 · 费用 " + std::to_string(S->cost) + "）";
    return I->name + "（" + std::to_string(S->attack) + "/" + std::to_string(S->health) + " · 费用 " +
           std::to_string(S->cost) + "）";
}

// ---------------------------------------------------------------------------
// Deck
// ---------------------------------------------------------------------------

Deck *Deck::createDeck() {
    Deck *d = Deck::create();
    d->membership();
    return d;
}

CardData *Deck::draw() {
    auto &cards = membership()->cards;
    if (cards.empty()) return nullptr;
    CardData *top = cards.back();
    cards.pop_back();
    return top;
}

CardData *Deck::peek() {
    auto &cards = membership()->cards;
    return cards.empty() ? nullptr : cards.back();
}

CardData *Deck::get(int index) {
    auto &cards = membership()->cards;
    if (index < 0 || static_cast<size_t>(index) >= cards.size()) return nullptr;
    return cards[static_cast<size_t>(index)];
}

void Deck::shuffle() {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::shuffle(membership()->cards.begin(), membership()->cards.end(), rng);
}

// ---------------------------------------------------------------------------
// Zone
// ---------------------------------------------------------------------------

Zone *Zone::createZone() {
    Zone *z = Zone::create();
    z->rect();
    z->filter();
    return z;
}

bool Zone::contains(float px, float py) {
    const auto *R = rect().operator->();
    return px >= R->x && px <= R->x + R->w && py >= R->y && py <= R->y + R->h;
}

bool Zone::accepts(CardData *card) {
    const auto *R = rect().operator->();
    if (!R->enabled || !card) return false;
    const auto &kinds = filter()->acceptKinds;
    if (kinds.empty()) return true;
    const std::string &kind = card->identity()->kind;
    for (const auto &k : kinds)
        if (k == kind) return true;
    return false;
}

void Zone::render(graphics::Graphics *gfx, bool showLabel) {
    auto *R = rect().operator->();
    gfx->drawSolidRect(R->x, R->y, R->w, R->h, glm::vec4(R->color, R->alpha));
    if (showLabel && gfx->getFont() && !R->label.empty()) {
        const glm::vec4 tint(clampf(R->color.r * 0.9f + 0.3f, 0.f, 1.f),
                             clampf(R->color.g * 0.9f + 0.3f, 0.f, 1.f),
                             clampf(R->color.b * 0.9f + 0.3f, 0.f, 1.f), 1.f);
        printCentered(gfx, R->label, R->x + R->w * 0.5f, R->y + R->h * 0.5f, 0.9f, tint);
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

void renderCard(graphics::Graphics *gfx, const CardData &cardRef, const LayoutConfig &cfg, bool back) {
    (void)cfg;
    CardData &card = const_cast<CardData &>(cardRef);
    auto projected = CardAttributeAdapter::project(card);
    if (!projected) {
        projected.ignore("card renderer skipped an invalid attribute projection");
        return;
    }
    const auto *L = card.layout().operator->();
    const auto *V = card.visual().operator->();
    const auto *I = card.identity().operator->();
    const auto *S = card.stats().operator->();

    const float w = L->w * L->scale;
    const float h = L->h * L->scale;
    const float x = L->x - w * 0.5f;
    const float y = L->y - h * 0.5f;
    const float a = L->alpha;

    const RenderTransform previous = gRenderTransform;
    gRenderTransform = {L->x, L->y, L->angle, true};

    if (back || !V->faceUp) {
        renderCardBack(gfx, x, y, w, h, L->angle, a);
        gRenderTransform = previous;
        return;
    }

    // 外框
    drawSolid(gfx, x, y, w, h, L->angle, glm::vec4(0.07f, 0.07f, 0.09f, a));
    const float inset = w * 0.035f;
    const float ix = x + inset;
    const float iy = y + inset;
    const float iw = w - inset * 2.f;
    const float ih = h - inset * 2.f;

    // 卡面底色
    drawSolid(gfx, ix, iy, iw, ih, L->angle, glm::vec4(V->tint, a));

    // 头部色带
    const float headerH = ih * 0.16f;
    drawSolid(gfx, ix, iy, iw, headerH, L->angle,
              glm::vec4(V->tint.r * 0.82f, V->tint.g * 0.82f, V->tint.b * 0.82f, a));

    // 卡图区域
    const float artY = iy + headerH;
    const float artH = ih * 0.34f;
    if (V->texture) {
        drawTexture(gfx, V->texture, ix + 2.f, artY + 2.f, iw - 4.f, artH - 4.f, L->angle,
                    glm::vec4(1.f, 1.f, 1.f, a));
    } else {
        drawSolid(gfx, ix + 2.f, artY + 2.f, iw - 4.f, artH - 4.f, L->angle,
                  glm::vec4(V->tint.r * 0.55f, V->tint.g * 0.55f, V->tint.b * 0.55f, a));
    }

    // 描述文字区
    const float bodyY = artY + artH;
    const float bodyH = iy + ih - bodyY;
    drawSolid(gfx, ix, bodyY, iw, bodyH, L->angle, glm::vec4(0.96f, 0.94f, 0.90f, a));
    drawSolid(gfx, ix + iw * 0.12f, bodyY + bodyH * 0.5f - 1.f, iw * 0.76f, 2.f, L->angle,
              glm::vec4(0.45f, 0.42f, 0.38f, a));

    // 数值宝石
    const float gem = w * 0.13f;
    drawGem(gfx, x + inset, iy, gem, L->angle, glm::vec3(1.00f, 0.78f, 0.25f), a);        // 费用（金）
    if (I->kind != "spell") {
        drawGem(gfx, x + inset, iy + ih - gem, gem, L->angle, glm::vec3(0.92f, 0.38f, 0.22f), a);  // 攻击（橙）
        drawGem(gfx, x + iw - inset - gem, iy + ih - gem, gem, L->angle, glm::vec3(0.30f, 0.78f, 0.42f), a);  // 生命（绿）
    }

    // 文字（可选：需 gfx 已 setFont）
    auto *font = gfx->getFont();
    if (font) {
        const float ts = 1.f * (w / 110.f);
        printCentered(gfx, std::to_string(S->cost), x + inset + gem * 0.5f, iy + gem * 0.55f, ts,
                      glm::vec4(0.12f, 0.08f, 0.05f, a));
        if (I->kind != "spell") {
            printCentered(gfx, std::to_string(S->attack), x + inset + gem * 0.5f, iy + ih - gem * 0.45f,
                          ts, glm::vec4(0.10f, 0.05f, 0.05f, a));
            printCentered(gfx, std::to_string(S->health), x + iw - inset - gem * 0.5f,
                          iy + ih - gem * 0.45f, ts, glm::vec4(0.05f, 0.10f, 0.05f, a));
        }
        printCentered(gfx, I->name, x + w * 0.5f, iy + headerH * 0.5f, ts * 0.8f,
                      glm::vec4(0.10f, 0.08f, 0.06f, a));
    }
    gRenderTransform = previous;
}

// ---------------------------------------------------------------------------
// Hand
// ---------------------------------------------------------------------------

Hand *Hand::createHand() {
    Hand *h = Hand::create();
    h->meta();
    h->membership();
    h->drag();
    return h;
}

void Hand::addCard(CardData *c) {
    membership()->cards.push_back(c);
    if (c) {
        auto *md = meta().operator->();
        c->state()->phase = c->visual()->disabled ? CardState::Disabled : CardState::Hand;
        c->layout()->w = md->config ? md->config->cardW : 110.f;
        c->layout()->h = md->config ? md->config->cardH : 150.f;
        c->layout()->scale = 0.01f;  // 从 0 生长出牌动画
        c->layout()->alpha = (c->visual()->disabled && md->config) ? md->config->disabledAlpha : 1.f;
    }
}

bool Hand::removeCard(CardData *c) {
    auto &cards = membership()->cards;
    for (size_t i = 0; i < cards.size(); ++i) {
        if (cards[i] == c) {
            cards.erase(cards.begin() + static_cast<ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

void Hand::clear() {
    membership()->cards.clear();
    auto *d = drag().operator->();
    d->card = nullptr;
    d->press = nullptr;
}

CardData *Hand::get(int index) {
    auto &cards = membership()->cards;
    if (index < 0 || static_cast<size_t>(index) >= cards.size()) return nullptr;
    return cards[static_cast<size_t>(index)];
}

CardData *Hand::find(const std::string &id) {
    for (auto *c : membership()->cards)
        if (c && c->identity()->id == id) return c;
    return nullptr;
}

CardData *Hand::pick(float px, float py) {
    auto &cards = membership()->cards;
    for (auto it = cards.rbegin(); it != cards.rend(); ++it) {
        if (*it && (*it)->hit(px, py)) return *it;
    }
    return nullptr;
}

void Hand::slotTransform(int i, int n, float &ox, float &oy, float &angle) {
    const LayoutConfig cfg = meta()->config ? *meta()->config : LayoutConfig();
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
    auto *md = meta().operator->();
    auto *d = drag().operator->();
    auto &cards = membership()->cards;
    const LayoutConfig cfg = md->config ? *md->config : LayoutConfig();
    const int n = static_cast<int>(cards.size());

    // 1) 布局 + 悬浮
    for (int i = 0; i < n; ++i) {
        CardData *card = cards[static_cast<size_t>(i)];
        if (!card || card == d->card) continue;

        float sx = 0.f, sy = 0.f, sa = 0.f;
        slotTransform(i, n, sx, sy, sa);

        bool hover = false;
        if (d->card == nullptr) hover = (card == pick(mx, my));
        auto *st = card->state().operator->();
        auto *vis = card->visual().operator->();
        auto *lay = card->layout().operator->();
        st->hovered = hover;
        if (vis->disabled) st->phase = CardState::Disabled;
        else if (st->dragging) st->phase = CardState::Dragging;
        else if (hover) st->phase = CardState::Hovered;
        else if (st->phase != CardState::Returning) st->phase = CardState::Hand;

        float targetScale = 1.f;
        float targetY = sy;
        float targetAngle = sa;
        if (hover) {
            targetY = sy - cfg.hoverLift;
            targetScale = cfg.hoverScale;
            if (cfg.hoverRotation) targetAngle = 0.f;
        }
        float k = frameRateK(hover ? cfg.hoverSpeed : cfg.motionSpeed, dt);
        lay->x = lerpf(lay->x, sx, k);
        lay->y = lerpf(lay->y, targetY, k);
        lay->scale = lerpf(lay->scale, targetScale, k);
        lay->angle = lerpf(lay->angle, targetAngle, k);
        lay->alpha = lerpf(lay->alpha, vis->disabled ? cfg.disabledAlpha : 1.f, k);
    }

    // 2) 拖拽状态机
    const bool press = down && !d->wasDown;
    const bool release = !down && d->wasDown;
    d->wasDown = down;

    if (press && md->interactive) {
        d->press = pick(mx, my);
        if (d->press && d->press->visual()->disabled) d->press = nullptr;
        d->pressX = mx;
        d->pressY = my;
    }

    if (down && d->press && !d->card) {
        const float dx = mx - d->pressX;
        const float dy = my - d->pressY;
        if (dx * dx + dy * dy > cfg.dragThreshold) {
            d->card = d->press;
            d->card->state()->dragging = true;
            d->card->state()->phase = CardState::Dragging;
            d->ox = mx - d->card->layout()->x;
            d->oy = my - d->card->layout()->y;
        }
    }

    if (d->card && down) {
        auto *lay = d->card->layout().operator->();
        lay->x = mx - d->ox;
        lay->y = my - d->oy;
        lay->scale = cfg.hoverScale;
        lay->angle = 0.f;
        lay->alpha = 1.f;
    }

    if (release) {
        if (d->card) {
            d->card->state()->dragging = false;
            d->card->state()->phase = CardState::Returning;
            CardData *dropped = d->card;
            d->card = nullptr;  // 归位；若已从手牌移除则自然消失
            for (Zone *z : zones) {
                if (z && z->contains(mx, my)) {
                    const std::string cardId = dropped ? dropped->identity()->id : "";
                    if (z->accepts(dropped)) {
                        out.push_back(CardEvent{"drop", md->owner, z->rect()->id, cardId, ""});
                    } else {
                        out.push_back(CardEvent{"dropRejected", md->owner, z->rect()->id, cardId,
                                                "kind_not_allowed"});
                    }
                    break;
                }
            }
        } else if (d->press) {
            out.push_back(CardEvent{"click", md->owner, "", d->press->identity()->id, ""});
        }
        d->press = nullptr;
    }
}

void Hand::render(graphics::Graphics *gfx) {
    auto *md = meta().operator->();
    const LayoutConfig cfg = md->config ? *md->config : LayoutConfig();
    const bool back = md->faceDown && !md->peek;
    const CardData *dragCard = drag()->card;
    for (auto *c : membership()->cards) {
        if (c && c != dragCard) renderCard(gfx, *c, cfg, back);
    }
    if (dragCard) renderCard(gfx, *dragCard, cfg, back);
}

}  // namespace eve::card
