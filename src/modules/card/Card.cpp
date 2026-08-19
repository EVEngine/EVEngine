#include "card/Card.h"

#include "common/Json.h"
#include "graphics/Graphics.h"

#include <simplesquirrel/simplesquirrel.hpp>
#include <squirrel.h>

#include <algorithm>
#include <cstdint>

namespace eve::card {

Module_IMPL(Card, new Card());

namespace {

using eve::json::Value;

bool parseDefinition(Value o, std::unordered_map<std::string, CardDefinition> &defs) {
    std::string id = o.getString("id");
    if (id.empty()) return false;
    CardDefinition def;
    def.id = id;
    def.name = o.getString("name", id);
    def.kind = o.getString("kind", "creature");
    def.cost = o.getInt("cost");
    def.attack = o.getInt("attack");
    def.health = o.getInt("health");
    auto tint = o.getFloatArray("tint");
    if (tint.size() >= 3) def.tint = glm::vec3(tint[0], tint[1], tint[2]);
    defs[id] = def;
    return true;
}

template <typename T>
T *resolve(const ecs::EntityHandle &h) {
    return static_cast<T *>(ecs::try_get(h));
}

void destroyHandles(std::vector<ecs::EntityHandle> &hs) {
    for (auto &h : hs) {
        if (ecs::Entity *e = ecs::try_get(h)) ecs::DestroyEntity(e);
    }
    hs.clear();
}

template <typename T>
void registerCppEntityClassForScript() {
    eve::registerCppEntityView(typeid(T *).hash_code(), [](ssq::Array &out) {
        HSQUIRRELVM vm = out.getHandle();
        sq_pushobject(vm, out.getRaw());
        ecs::Table *table = ecs::current();
        if (table == nullptr) {
            sq_pop(vm, 1);
            return;
        }
        ecs::IComponentManager &cm = table->getOrCreateManager<T>();
        auto *reg = cm.getOrCreateRegistryComponentBuffer<T>();
        std::vector<ecs::IComponentBuffer *> stack;
        stack.push_back(reg);
        while (!stack.empty()) {
            ecs::IComponentBuffer *buf = stack.back();
            stack.pop_back();
            auto *r = dynamic_cast<ecs::IRegistryComponentBuffer *>(buf);
            if (r != nullptr) {
                for (uint32_t i = 0; i < r->entity_count(); ++i) {
                    ecs::Entity *ent = r->entity_at(i);
                    if (ent != nullptr && ecs::is_entity_visible(ent)) {
                        ssq::detail::pushByPtr<T>(vm, static_cast<T *>(ent));
                        sq_arrayappend(vm, -2);
                    }
                }
            }
            if (buf->children != nullptr) stack.push_back(buf->children);
            if (buf->next != nullptr) stack.push_back(buf->next);
        }
        sq_pop(vm, 1);
    });
}

}  // namespace

Card::~Card() {
    destroyHandles(hands_);
    destroyHandles(decks_);
    destroyHandles(zones_);
    destroyHandles(cards_);
    activeDeck_ = nullptr;
    activeConfig_ = nullptr;
}

// ---------------------------------------------------------------------------
// 卡牌类型定义
// ---------------------------------------------------------------------------

int Card::registerCardsFromJson(const std::string &json) {
    const eve::json::Document doc = eve::json::Document::parse(json);
    if (!doc.valid()) return 0;
    const Value root = doc.root();
    int n = 0;
    if (root.isArray()) {
        for (size_t i = 0; i < root.size(); ++i)
            if (parseDefinition(root.at(i), defs_)) ++n;
    } else if (root.isObject()) {
        if (parseDefinition(root, defs_)) ++n;
    }
    return n;
}

void Card::clearCardDefinitions() { defs_.clear(); }

int Card::getCardDefinitionCount() { return static_cast<int>(defs_.size()); }

bool Card::hasCardDefinition(const std::string &id) { return defs_.count(id) != 0; }

const CardDefinition *Card::findDef(const std::string &id) const {
    auto it = defs_.find(id);
    return it == defs_.end() ? nullptr : &it->second;
}

std::string Card::getCardDefinitionName(const std::string &id) {
    const auto *d = findDef(id);
    return d ? d->name : std::string{};
}

std::string Card::getCardDefinitionKind(const std::string &id) {
    const auto *d = findDef(id);
    return d ? d->kind : std::string{};
}

int Card::getCardDefinitionCost(const std::string &id) {
    const auto *d = findDef(id);
    return d ? d->cost : 0;
}

int Card::getCardDefinitionAttack(const std::string &id) {
    const auto *d = findDef(id);
    return d ? d->attack : 0;
}

int Card::getCardDefinitionHealth(const std::string &id) {
    const auto *d = findDef(id);
    return d ? d->health : 0;
}

float Card::getCardDefinitionTintR(const std::string &id) {
    const auto *d = findDef(id);
    return d ? d->tint.r : 0.f;
}

float Card::getCardDefinitionTintG(const std::string &id) {
    const auto *d = findDef(id);
    return d ? d->tint.g : 0.f;
}

float Card::getCardDefinitionTintB(const std::string &id) {
    const auto *d = findDef(id);
    return d ? d->tint.b : 0.f;
}

// ---------------------------------------------------------------------------
// 工厂
// ---------------------------------------------------------------------------

LayoutConfig *Card::newConfig() {
    auto cfg = std::make_unique<LayoutConfig>();
    LayoutConfig *raw = cfg.get();
    configs_.push_back(std::move(cfg));
    if (!activeConfig_) activeConfig_ = raw;
    return raw;
}

CardData *Card::newCard(const std::string &defId) {
    const CardDefinition *d = findDef(defId);
    if (!d) return nullptr;
    CardData *c = CardData::createCard();
    c->identity()->id = defId + "#" + std::to_string(nextInstance_++);
    c->identity()->name = d->name;
    c->identity()->kind = d->kind;
    c->stats()->cost = d->cost;
    c->stats()->attack = d->attack;
    c->stats()->health = d->health;
    c->visual()->tint = d->tint;
    cards_.push_back(ecs::handle_of(c));
    return c;
}

Deck *Card::newDeck() {
    Deck *d = Deck::createDeck();
    decks_.push_back(ecs::handle_of(d));
    activeDeck_ = d;
    return d;
}

Zone *Card::newZone(const std::string &id, const std::string &label, float x, float y, float w, float h) {
    Zone *z = Zone::createZone();
    auto *R = z->rect().operator->();
    R->id = id;
    R->label = label;
    R->x = x;
    R->y = y;
    R->w = w;
    R->h = h;
    zones_.push_back(ecs::handle_of(z));
    return z;
}

Hand *Card::newHand(LayoutConfig *cfg) {
    Hand *h = Hand::createHand();
    h->meta()->config = cfg;
    hands_.push_back(ecs::handle_of(h));
    return h;
}

// ---------------------------------------------------------------------------
// 游戏状态
// ---------------------------------------------------------------------------

void Card::setConfig(LayoutConfig *cfg) {
    if (cfg) activeConfig_ = cfg;
}

LayoutConfig *Card::getConfig() const { return activeConfig_; }

int Card::handCount() const { return static_cast<int>(hands_.size()); }

Hand *Card::getHand(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= hands_.size()) return nullptr;
    return resolve<Hand>(hands_[static_cast<size_t>(index)]);
}

Hand *Card::findHand(const std::string &owner) const {
    for (const auto &h : hands_) {
        Hand *hand = resolve<Hand>(h);
        if (hand && hand->meta()->owner == owner) return hand;
    }
    return nullptr;
}

int Card::zoneCount() const { return static_cast<int>(zones_.size()); }

Zone *Card::getZone(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= zones_.size()) return nullptr;
    return resolve<Zone>(zones_[static_cast<size_t>(index)]);
}

Deck *Card::getDeck() const { return activeDeck_; }

CardData *Card::drawCard(const std::string &handOwner) {
    Hand *h = findHand(handOwner);
    if (!h || !activeDeck_) return nullptr;
    CardData *c = activeDeck_->draw();
    if (!c) return nullptr;
    if (activeConfig_) {
        c->layout()->x = activeConfig_->deckX;
        c->layout()->y = activeConfig_->deckY;
    }
    c->state()->phase = CardState::Deck;
    h->addCard(c);
    return c;
}

// ---------------------------------------------------------------------------
// 每帧
// ---------------------------------------------------------------------------

void Card::update(float dt, float mx, float my, bool down) {
    std::vector<Zone *> zonePtrs;
    zonePtrs.reserve(zones_.size());
    for (auto &h : zones_) {
        if (Zone *z = resolve<Zone>(h)) zonePtrs.push_back(z);
    }
    for (auto &h : hands_) {
        if (Hand *hand = resolve<Hand>(h)) hand->update(dt, mx, my, down, zonePtrs, events_);
    }
}

void Card::render(graphics::Graphics *gfx) {
    if (activeConfig_ && activeConfig_->showZones) {
        for (auto &h : zones_) {
            if (Zone *z = resolve<Zone>(h)) z->render(gfx, true);
        }
    }
    for (auto &h : hands_) {
        if (Hand *hand = resolve<Hand>(h)) hand->render(gfx);
    }
}

void Card::renderDeck(graphics::Graphics *gfx) {
    if (!activeDeck_ || !activeConfig_) return;
    const LayoutConfig &cfg = *activeConfig_;
    const int c = activeDeck_->count();
    if (c <= 0) return;
    const float w = cfg.cardW * 0.9f;
    const float h = cfg.cardH * 0.9f;
    for (int i = 0; i < 3 && i < c; ++i) {
        const float off = static_cast<float>(i) * 3.f;
        renderCardBack(gfx, cfg.deckX - w * 0.5f + off, cfg.deckY - h * 0.5f - off, w, h, 0.f, 1.f);
    }
    if (gfx->getFont())
        printCentered(gfx, std::to_string(c), cfg.deckX, cfg.deckY - h * 0.5f - 12.f, 1.2f,
                      glm::vec4(0.95f, 0.9f, 0.8f, 1.f));
}

// ---------------------------------------------------------------------------
// 事件
// ---------------------------------------------------------------------------

void Card::clearEvents() { events_.clear(); }

int Card::getEventCount() const { return static_cast<int>(events_.size()); }

std::string Card::getEventType(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= events_.size()) return {};
    return events_[static_cast<size_t>(index)].type;
}

std::string Card::getEventHand(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= events_.size()) return {};
    return events_[static_cast<size_t>(index)].hand;
}

std::string Card::getEventZone(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= events_.size()) return {};
    return events_[static_cast<size_t>(index)].zoneId;
}

std::string Card::getEventCardId(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= events_.size()) return {};
    return events_[static_cast<size_t>(index)].cardId;
}

std::string Card::getEventReason(int index) const {
    if (index < 0 || static_cast<size_t>(index) >= events_.size()) return {};
    return events_[static_cast<size_t>(index)].reason;
}

// ---------------------------------------------------------------------------
// 脚本绑定
// ---------------------------------------------------------------------------

void Card::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Card::create, false);
    expose(cls);

    registerCppEntityClassForScript<CardData>();
    registerCppEntityClassForScript<Hand>();
    registerCppEntityClassForScript<Deck>();
    registerCppEntityClassForScript<Zone>();

    // LayoutConfig —— 全部布局参数（UiCard Configs）
    auto cfgCls = table.addClass<LayoutConfig>(
        "LayoutConfig", std::function<LayoutConfig *()>([]() -> LayoutConfig * { return nullptr; }), false);
    cfgCls.addFunc("getCardW", [](LayoutConfig *c) -> float { return c ? c->cardW : 0.f; });
    cfgCls.addFunc("setCardW", [](LayoutConfig *c, float v) { if (c) c->cardW = v; });
    cfgCls.addFunc("getCardH", [](LayoutConfig *c) -> float { return c ? c->cardH : 0.f; });
    cfgCls.addFunc("setCardH", [](LayoutConfig *c, float v) { if (c) c->cardH = v; });
    cfgCls.addFunc("getSpacing", [](LayoutConfig *c) -> float { return c ? c->spacing : 0.f; });
    cfgCls.addFunc("setSpacing", [](LayoutConfig *c, float v) { if (c) c->spacing = v; });
    cfgCls.addFunc("getHandX", [](LayoutConfig *c) -> float { return c ? c->handX : 0.f; });
    cfgCls.addFunc("setHandX", [](LayoutConfig *c, float v) { if (c) c->handX = v; });
    cfgCls.addFunc("getHandY", [](LayoutConfig *c) -> float { return c ? c->handY : 0.f; });
    cfgCls.addFunc("setHandY", [](LayoutConfig *c, float v) { if (c) c->handY = v; });
    cfgCls.addFunc("getArcHeight", [](LayoutConfig *c) -> float { return c ? c->arcHeight : 0.f; });
    cfgCls.addFunc("setArcHeight", [](LayoutConfig *c, float v) { if (c) c->arcHeight = v; });
    cfgCls.addFunc("getRotationAngle", [](LayoutConfig *c) -> float { return c ? c->rotationAngle : 0.f; });
    cfgCls.addFunc("setRotationAngle", [](LayoutConfig *c, float v) { if (c) c->rotationAngle = v; });
    cfgCls.addFunc("getHoverRotation", [](LayoutConfig *c) -> bool { return c ? c->hoverRotation : false; });
    cfgCls.addFunc("setHoverRotation", [](LayoutConfig *c, bool v) { if (c) c->hoverRotation = v; });
    cfgCls.addFunc("getHoverScale", [](LayoutConfig *c) -> float { return c ? c->hoverScale : 0.f; });
    cfgCls.addFunc("setHoverScale", [](LayoutConfig *c, float v) { if (c) c->hoverScale = v; });
    cfgCls.addFunc("getHoverLift", [](LayoutConfig *c) -> float { return c ? c->hoverLift : 0.f; });
    cfgCls.addFunc("setHoverLift", [](LayoutConfig *c, float v) { if (c) c->hoverLift = v; });
    cfgCls.addFunc("getHoverSpeed", [](LayoutConfig *c) -> float { return c ? c->hoverSpeed : 0.f; });
    cfgCls.addFunc("setHoverSpeed", [](LayoutConfig *c, float v) { if (c) c->hoverSpeed = v; });
    cfgCls.addFunc("getMotionSpeed", [](LayoutConfig *c) -> float { return c ? c->motionSpeed : 0.f; });
    cfgCls.addFunc("setMotionSpeed", [](LayoutConfig *c, float v) { if (c) c->motionSpeed = v; });
    cfgCls.addFunc("getDisabledAlpha", [](LayoutConfig *c) -> float { return c ? c->disabledAlpha : 0.f; });
    cfgCls.addFunc("setDisabledAlpha", [](LayoutConfig *c, float v) { if (c) c->disabledAlpha = v; });
    cfgCls.addFunc("getShowZones", [](LayoutConfig *c) -> bool { return c ? c->showZones : false; });
    cfgCls.addFunc("setShowZones", [](LayoutConfig *c, bool v) { if (c) c->showZones = v; });
    cfgCls.addFunc("getDeckX", [](LayoutConfig *c) -> float { return c ? c->deckX : 0.f; });
    cfgCls.addFunc("setDeckX", [](LayoutConfig *c, float v) { if (c) c->deckX = v; });
    cfgCls.addFunc("getDeckY", [](LayoutConfig *c) -> float { return c ? c->deckY : 0.f; });
    cfgCls.addFunc("setDeckY", [](LayoutConfig *c, float v) { if (c) c->deckY = v; });
    cfgCls.addFunc("getDragThreshold", [](LayoutConfig *c) -> float { return c ? c->dragThreshold : 0.f; });
    cfgCls.addFunc("setDragThreshold", [](LayoutConfig *c, float v) { if (c) c->dragThreshold = v; });

    // CardData
    auto cardCls = table.addClass<CardData>(
        "CardData", std::function<CardData *()>([]() -> CardData * { return nullptr; }), false);
    cardCls.addFunc("getId", [](CardData *c) -> std::string { return c ? c->identity()->id : std::string{}; });
    cardCls.addFunc("setId", [](CardData *c, const std::string &v) { if (c) c->identity()->id = v; });
    cardCls.addFunc("getName", [](CardData *c) -> std::string { return c ? c->identity()->name : std::string{}; });
    cardCls.addFunc("setName", [](CardData *c, const std::string &v) { if (c) c->identity()->name = v; });
    cardCls.addFunc("getKind", [](CardData *c) -> std::string { return c ? c->identity()->kind : std::string{}; });
    cardCls.addFunc("setKind", [](CardData *c, const std::string &v) { if (c) c->identity()->kind = v; });
    cardCls.addFunc("getCost", [](CardData *c) -> int { return c ? c->stats()->cost : 0; });
    cardCls.addFunc("setCost", [](CardData *c, int v) { if (c) c->stats()->cost = v; });
    cardCls.addFunc("getAttack", [](CardData *c) -> int { return c ? c->stats()->attack : 0; });
    cardCls.addFunc("setAttack", [](CardData *c, int v) { if (c) c->stats()->attack = v; });
    cardCls.addFunc("getHealth", [](CardData *c) -> int { return c ? c->stats()->health : 0; });
    cardCls.addFunc("setHealth", [](CardData *c, int v) { if (c) c->stats()->health = v; });
    cardCls.addFunc("isFaceUp", [](CardData *c) -> bool { return c ? c->visual()->faceUp : true; });
    cardCls.addFunc("setFaceUp", [](CardData *c, bool v) { if (c) c->visual()->faceUp = v; });
    cardCls.addFunc("isDisabled", [](CardData *c) -> bool { return c ? c->visual()->disabled : false; });
    cardCls.addFunc("setDisabled", [](CardData *c, bool v) { if (c) c->visual()->disabled = v; });
    cardCls.addFunc("getState", [](CardData *c) -> std::string {
        return c ? cardStateName(c->state()->phase) : std::string{};
    });
    cardCls.addFunc("setState", [](CardData *c, const std::string &v) {
        if (!c) return;
        auto *st = c->state().operator->();
        if (v == "deck") st->phase = CardState::Deck;
        else if (v == "hand") st->phase = CardState::Hand;
        else if (v == "hovered") st->phase = CardState::Hovered;
        else if (v == "dragging") st->phase = CardState::Dragging;
        else if (v == "returning") st->phase = CardState::Returning;
        else if (v == "played") st->phase = CardState::Played;
        else if (v == "discarded") st->phase = CardState::Discarded;
        else if (v == "disabled") st->phase = CardState::Disabled;
    });
    cardCls.addFunc("getTintR", [](CardData *c) -> float { return c ? c->visual()->tint.r : 0.f; });
    cardCls.addFunc("getTintG", [](CardData *c) -> float { return c ? c->visual()->tint.g : 0.f; });
    cardCls.addFunc("getTintB", [](CardData *c) -> float { return c ? c->visual()->tint.b : 0.f; });
    cardCls.addFunc("setTint", [](CardData *c, float r, float g, float b) {
        if (c) c->visual()->tint = glm::vec3(r, g, b);
    });
    cardCls.addFunc("setArt", [](CardData *c, graphics::Texture *t) { if (c) c->visual()->texture = t; });
    cardCls.addFunc("getArt", [](CardData *c) -> graphics::Texture * { return c ? c->visual()->texture : nullptr; });
    cardCls.addFunc("getX", [](CardData *c) -> float { return c ? c->layout()->x : 0.f; });
    cardCls.addFunc("getY", [](CardData *c) -> float { return c ? c->layout()->y : 0.f; });
    cardCls.addFunc("getW", [](CardData *c) -> float { return c ? c->layout()->w : 0.f; });
    cardCls.addFunc("getH", [](CardData *c) -> float { return c ? c->layout()->h : 0.f; });
    cardCls.addFunc("getScale", [](CardData *c) -> float { return c ? c->layout()->scale : 0.f; });
    cardCls.addFunc("getAlpha", [](CardData *c) -> float { return c ? c->layout()->alpha : 0.f; });
    cardCls.addFunc("isHovered", [](CardData *c) -> bool { return c ? c->state()->hovered : false; });
    cardCls.addFunc("isDragging", [](CardData *c) -> bool { return c ? c->state()->dragging : false; });
    cardCls.addFunc("hit", [](CardData *c, float px, float py) -> bool { return c && c->hit(px, py); });
    cardCls.addFunc("describe", [](CardData *c) -> std::string { return c ? c->describe() : std::string{}; });

    // Deck
    auto deckCls = table.addClass<Deck>(
        "Deck", std::function<Deck *()>([]() -> Deck * { return nullptr; }), false);
    deckCls.addFunc("push", &Deck::push);
    deckCls.addFunc("draw", &Deck::draw);
    deckCls.addFunc("peek", &Deck::peek);
    deckCls.addFunc("count", &Deck::count);
    deckCls.addFunc("isEmpty", &Deck::isEmpty);
    deckCls.addFunc("clear", &Deck::clear);
    deckCls.addFunc("shuffle", &Deck::shuffle);
    deckCls.addFunc("getCard", &Deck::get);

    // Zone
    auto zoneCls = table.addClass<Zone>(
        "Zone", std::function<Zone *()>([]() -> Zone * { return nullptr; }), false);
    zoneCls.addFunc("getId", [](Zone *z) -> std::string { return z ? z->rect()->id : std::string{}; });
    zoneCls.addFunc("setId", [](Zone *z, const std::string &v) { if (z) z->rect()->id = v; });
    zoneCls.addFunc("getLabel", [](Zone *z) -> std::string { return z ? z->rect()->label : std::string{}; });
    zoneCls.addFunc("setLabel", [](Zone *z, const std::string &v) { if (z) z->rect()->label = v; });
    zoneCls.addFunc("getX", [](Zone *z) -> float { return z ? z->rect()->x : 0.f; });
    zoneCls.addFunc("getY", [](Zone *z) -> float { return z ? z->rect()->y : 0.f; });
    zoneCls.addFunc("getW", [](Zone *z) -> float { return z ? z->rect()->w : 0.f; });
    zoneCls.addFunc("getH", [](Zone *z) -> float { return z ? z->rect()->h : 0.f; });
    zoneCls.addFunc("setRect", [](Zone *z, float x, float y, float w, float h) {
        if (!z) return;
        auto *R = z->rect().operator->();
        R->x = x; R->y = y; R->w = w; R->h = h;
    });
    zoneCls.addFunc("getColorR", [](Zone *z) -> float { return z ? z->rect()->color.r : 0.f; });
    zoneCls.addFunc("getColorG", [](Zone *z) -> float { return z ? z->rect()->color.g : 0.f; });
    zoneCls.addFunc("getColorB", [](Zone *z) -> float { return z ? z->rect()->color.b : 0.f; });
    zoneCls.addFunc("setColor", [](Zone *z, float r, float g, float b) {
        if (z) z->rect()->color = glm::vec3(r, g, b);
    });
    zoneCls.addFunc("getAlpha", [](Zone *z) -> float { return z ? z->rect()->alpha : 0.f; });
    zoneCls.addFunc("setAlpha", [](Zone *z, float v) { if (z) z->rect()->alpha = v; });
    zoneCls.addFunc("isEnabled", [](Zone *z) -> bool { return z ? z->rect()->enabled : false; });
    zoneCls.addFunc("setEnabled", [](Zone *z, bool v) { if (z) z->rect()->enabled = v; });
    zoneCls.addFunc("addAcceptKind", [](Zone *z, const std::string &k) { if (z) z->filter()->acceptKinds.push_back(k); });
    zoneCls.addFunc("clearAcceptKinds", [](Zone *z) { if (z) z->filter()->acceptKinds.clear(); });
    zoneCls.addFunc("accepts", [](Zone *z, CardData *c) -> bool { return z && z->accepts(c); });
    zoneCls.addFunc("contains", [](Zone *z, float px, float py) -> bool { return z && z->contains(px, py); });
    zoneCls.addFunc("render", [](Zone *z, graphics::Graphics *gfx, bool showLabel) {
        if (z) z->render(gfx, showLabel);
    });

    // Hand
    auto handCls = table.addClass<Hand>(
        "Hand", std::function<Hand *()>([]() -> Hand * { return nullptr; }), false);
    handCls.addFunc("getOwner", [](Hand *h) -> std::string { return h ? h->meta()->owner : std::string{}; });
    handCls.addFunc("setOwner", [](Hand *h, const std::string &v) { if (h) h->meta()->owner = v; });
    handCls.addFunc("getConfig", [](Hand *h) -> LayoutConfig * { return h ? h->meta()->config : nullptr; });
    handCls.addFunc("setConfig", [](Hand *h, LayoutConfig *c) { if (h) h->meta()->config = c; });
    handCls.addFunc("isFaceDown", [](Hand *h) -> bool { return h ? h->meta()->faceDown : false; });
    handCls.addFunc("setFaceDown", [](Hand *h, bool v) { if (h) h->meta()->faceDown = v; });
    handCls.addFunc("isPeek", [](Hand *h) -> bool { return h ? h->meta()->peek : false; });
    handCls.addFunc("setPeek", [](Hand *h, bool v) { if (h) h->meta()->peek = v; });
    handCls.addFunc("isInteractive", [](Hand *h) -> bool { return h ? h->meta()->interactive : true; });
    handCls.addFunc("setInteractive", [](Hand *h, bool v) { if (h) h->meta()->interactive = v; });
    handCls.addFunc("addCard", &Hand::addCard);
    handCls.addFunc("removeCard", &Hand::removeCard);
    handCls.addFunc("clear", &Hand::clear);
    handCls.addFunc("count", &Hand::count);
    handCls.addFunc("getCard", &Hand::get);
    handCls.addFunc("findCard", &Hand::find);
    handCls.addFunc("pickCard", &Hand::pick);
    handCls.addFunc("render", [](Hand *h, graphics::Graphics *gfx) { if (h) h->render(gfx); });
}

void Card::expose(ssq::Class &cls) {
    cls.addFunc("registerCardsFromJson", &Card::registerCardsFromJson);
    cls.addFunc("clearCardDefinitions", &Card::clearCardDefinitions);
    cls.addFunc("getCardDefinitionCount", &Card::getCardDefinitionCount);
    cls.addFunc("hasCardDefinition", &Card::hasCardDefinition);
    cls.addFunc("getCardDefinitionName", &Card::getCardDefinitionName);
    cls.addFunc("getCardDefinitionKind", &Card::getCardDefinitionKind);
    cls.addFunc("getCardDefinitionCost", &Card::getCardDefinitionCost);
    cls.addFunc("getCardDefinitionAttack", &Card::getCardDefinitionAttack);
    cls.addFunc("getCardDefinitionHealth", &Card::getCardDefinitionHealth);
    cls.addFunc("getCardDefinitionTintR", &Card::getCardDefinitionTintR);
    cls.addFunc("getCardDefinitionTintG", &Card::getCardDefinitionTintG);
    cls.addFunc("getCardDefinitionTintB", &Card::getCardDefinitionTintB);
    cls.addFunc("newConfig", &Card::newConfig);
    cls.addFunc("newCard", &Card::newCard);
    cls.addFunc("newDeck", &Card::newDeck);
    cls.addFunc("newZone", &Card::newZone);
    cls.addFunc("newHand", &Card::newHand);
    cls.addFunc("setConfig", &Card::setConfig);
    cls.addFunc("getConfig", &Card::getConfig);
    cls.addFunc("handCount", &Card::handCount);
    cls.addFunc("getHand", &Card::getHand);
    cls.addFunc("findHand", &Card::findHand);
    cls.addFunc("zoneCount", &Card::zoneCount);
    cls.addFunc("getZone", &Card::getZone);
    cls.addFunc("getDeck", &Card::getDeck);
    cls.addFunc("drawCard", &Card::drawCard);
    cls.addFunc("update", &Card::update);
    cls.addFunc("render", &Card::render);
    cls.addFunc("renderDeck", &Card::renderDeck);
    cls.addFunc("clearEvents", &Card::clearEvents);
    cls.addFunc("getEventCount", &Card::getEventCount);
    cls.addFunc("getEventType", &Card::getEventType);
    cls.addFunc("getEventHand", &Card::getEventHand);
    cls.addFunc("getEventZone", &Card::getEventZone);
    cls.addFunc("getEventCardId", &Card::getEventCardId);
    cls.addFunc("getEventReason", &Card::getEventReason);
}

}  // namespace eve::card
