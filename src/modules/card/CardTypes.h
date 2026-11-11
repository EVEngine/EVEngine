#pragma once
#include "common/Export.h"


/**
 * @brief 卡牌游戏 UI 工具模块：数据模型与渲染/交互逻辑（功能参考 ycarowr/UiCard）。
 * 模块入口 eve.Card() 见 Card.h；本文件是 CardData/Deck/Zone/Hand 等 ECS 实体与布局渲染实现。
 */

#include "attributes/AttributeProjection.h"
#include "card/CardEffects.h"
#include "common/Condition.h"
#include "common/ECS.h"
#include "common/definitions/DefinitionRuntime.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace eve::graphics {
class Graphics;
class Texture;
}  // namespace eve::graphics

namespace eve::card {

/** @brief 单张卡牌的运行时状态。 */
enum class CardState : uint8_t {
    Deck,
    Hand,
    Hovered,
    Dragging,
    Returning,
    Played,
    Discarded,
    Disabled,
};

/** @brief 状态枚举的字符串名（用于脚本/调试）。 */
EVENGINE_API_WORLD const char *cardStateName(CardState state);

/** @brief UiCard 风格的布局配置（脚本可实时修改，对应 UiCard 的 Configs 面板）。 */
struct LayoutConfig {
    /** @brief 卡牌宽/高（像素）。 */
    float cardW = 110.f;
    float cardH = 150.f;
    /** @brief 手牌水平间距。 */
    float spacing = 26.f;
    /** @brief 手牌扇形中心 X / 基准高度 Y。 */
    float handX = 640.f;
    float handY = 640.f;
    /** @brief 扇形弧度（两端上翘高度）。 */
    float arcHeight = 46.f;
    /** @brief 手牌两端最大旋转角（度）。 */
    float rotationAngle = 14.f;
    /** @brief 悬浮时是否恢复为水平。 */
    bool  hoverRotation = true;
    /** @brief 悬浮缩放/上移像素/运动速度。 */
    float hoverScale = 1.22f;
    float hoverLift = 46.f;
    float hoverSpeed = 0.18f;
    /** @brief 通用归位运动速度。 */
    float motionSpeed = 0.12f;
    /**
     * @brief Face flip lerp speed (same 60fps-normalized units as motionSpeed).
     * Drives the animated 0..1 face-down progress used by 2D squash and 3D adapters.
     */
    float flipSpeed = 0.18f;
    /** @brief Peak lift (logical pixels) at the edge-on midpoint of a flip. */
    float flipLift = 28.f;
    /**
     * @brief Flip hinge: 0 = vertical (width foreshortens / rotate about local up),
     * 1 = horizontal (height foreshortens / rotate about local right).
     */
    int flipAxis = 0;
    /** @brief 禁用牌透明度。 */
    float disabledAlpha = 0.35f;
    /** @brief 是否绘制落牌区。 */
    bool  showZones = true;
    /** @brief 牌库堆绘制位置。 */
    float deckX = 360.f;
    float deckY = 640.f;
    /** @brief 拖拽触发距离（像素）。 */
    float dragThreshold = 64.f;
};

/** @brief 卡牌类型定义（registerCardsFromJson 注册）。 */
struct CardDefinition {
    std::string id;
    std::string name;
    std::string kind = "creature";  // "creature" | "spell" | ...
    int         cost = 0;
    int         attack = 0;
    int         health = 0;
    glm::vec3   tint{0.62f, 0.50f, 0.40f};
    /** @brief Side-effect-free condition tree required before this card can play. */
    eve::condition::Condition playCondition;
    /** @brief Definition tags available to HasTag nodes. */
    std::vector<std::string> tags;
};

/** @brief 单张卡牌：ECS 实体，数据拆成 Identity / Stats / Visual / Layout / State。 */
class EVENGINE_API_WORLD CardData : public ecs::Entity {
public:
    /** @brief Entity. */
    ENTITY(CardData, ecs::Entity)

    /** @brief Release. */
    void release() override { ecs::DestroyEntity(this); }

    /** @brief 稳定实例 id、显示名、种类。 */
    struct Identity {
        /** @brief Stable runtime instance identifier, unique within the owning Card module. */
        std::string id;
        /** @brief Registered definition identifier used to construct this instance. */
        std::string definitionId;
        std::string name;
        std::string kind = "creature";
    };
    /** @brief 费用 / 攻击 / 生命。 */
    struct Stats {
        int cost = 0;
        int attack = 0;
        int health = 0;
    };
    /**
     * @brief Canonical selected card stats backed by the attributes module.
     *
     * `Stats` is a one-way compatibility projection refreshed by
     * CardAttributeAdapter. Card layout, interaction state and card-zone
     * membership deliberately remain outside this component.
     */
    struct Attributes {
        eve::attributes::AttributeProjection values;
    };
    /** @brief Canonical effect lifecycle and card combat target state. */
    struct Effects {
        CardEffectAdapter values;
    };
    /** @brief 朝向、禁用、色调、卡图。 */
    struct Visual {
        /** @brief Logical face target; presentation animates `flip` toward it. */
        bool faceUp = true;
        /**
         * @brief Animated face-down progress in [0, 1].
         * 0 = fully face-up, 1 = fully face-down. Midpoint (0.5) is edge-on:
         * swap front/back art when showingFront flips across that threshold.
         */
        float              flip     = 0.f;
        bool disabled = false;
        glm::vec3 tint{0.62f, 0.50f, 0.40f};
        graphics::Texture *texture = nullptr;
    };
    /** @brief 运行时布局（由 Hand 维护）：中心坐标与变换。 */
    struct Layout {
        float x = 0.f;
        float y = 0.f;
        float w = 0.f;
        float h = 0.f;
        float angle = 0.f;
        float scale = 1.f;
        float alpha = 1.f;
    };
    /** @brief 运行时交互状态。 */
    struct State {
        CardState phase = CardState::Deck;
        bool hovered = false;
        bool dragging = false;
    };

    /**
     * @brief Definition link and reload policy for this card instance.
     *
     * This is a cold ECS component: `identity` is authoritative for the
     * definition incarnation, while the legacy `Identity::definitionId` is a
     * read-only compatibility projection. The component is rebuilt by the
     * CardDefinitionRuntime adapter after a successful hot reload.
     */
    struct DefinitionBinding {
        eve::definition::InstanceIdentity identity;
        eve::definition::ReloadPolicy     reloadPolicy = eve::definition::ReloadPolicy::KeepInstanceValues;
        bool                              active       = true;
    };

    COMPONENT(Identity, identity)
    COMPONENT(Stats, stats)
    COMPONENT(Attributes, attributes)
    COMPONENT(Effects, effects)
    COMPONENT(Visual, visual)
    COMPONENT(Layout, layout)
    COMPONENT(State, state)
    COMPONENT(DefinitionBinding, definitionBinding)

    /** @brief 创建并触摸全部组件。 */
    static CardData *createCard();

    /** @brief 以当前中心 + 缩放判断点是否命中。 */
    bool hit(float px, float py);
    /** @brief 卡牌描述字符串（调试用）。 */
    std::string describe();
};

/** @brief 牌库（栈顶在末尾）。 */
class EVENGINE_API_WORLD Deck : public ecs::Entity {
public:
    /** @brief Entity. */
    ENTITY(Deck, ecs::Entity)

    /** @brief Release. */
    void release() override { ecs::DestroyEntity(this); }

    /** @brief 有序牌堆；栈顶在 vector 末尾。 */
    struct Membership {
        std::vector<CardData *> cards;
    };

    COMPONENT(Membership, membership)

    /** @brief 创建并触摸 Membership。 */
    static Deck *createDeck();

    /** @brief 将卡牌压入栈顶（末尾）。 */
    void push(CardData *c) { membership()->cards.push_back(c); }
    /** @brief 弹出栈顶卡牌（末尾）；空牌库返回 nullptr。 */
    CardData *draw();
    /** @brief 查看栈顶卡牌（不弹出）。 */
    CardData *peek();
    /** @brief 清空牌库。 */
    void clear() { membership()->cards.clear(); }
    /** @brief 洗牌。 */
    void shuffle();
    /** @brief 牌库数量/是否为空/按下标取牌。 */
    int  count() { return static_cast<int>(membership()->cards.size()); }
    bool isEmpty() { return membership()->cards.empty(); }
    CardData *get(int index);
};

/** @brief 落牌区（手牌区 / 出牌区 / 弃牌区），用于拖放命中判定。 */
class EVENGINE_API_WORLD Zone : public ecs::Entity {
public:
    /** @brief Entity. */
    ENTITY(Zone, ecs::Entity)

    /** @brief Release. */
    void release() override { ecs::DestroyEntity(this); }

    /** @brief 区域矩形与绘制参数。 */
    struct Rect {
        std::string id;
        std::string label;
        float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
        glm::vec3 color{0.30f, 0.60f, 0.30f};
        float alpha = 0.16f;
        bool  enabled = true;
    };
    /** @brief 接受的卡牌 kind；空 = 接受任意 kind。 */
    struct Filter {
        std::vector<std::string> acceptKinds;
    };

    COMPONENT(Rect, rect)
    COMPONENT(Filter, filter)

    /** @brief 创建并触摸组件。 */
    static Zone *createZone();

    /** @brief 点是否落在区域内。 */
    bool contains(float px, float py);
    /** @brief 该区域是否接受这张卡。 */
    bool accepts(CardData *card);
    /** @brief 绘制落牌区。 */
    void render(graphics::Graphics *gfx, bool showLabel);
};

/** @brief 一次交互事件。 */
struct CardEvent {
    /** @brief 事件类型："click" | "drop"。 */
    std::string type;
    /** @brief 来源手牌 owner。 */
    std::string hand;
    /** @brief drop 目标的落牌区 id（click 为空）。 */
    std::string zoneId;
    std::string cardId;
    /** @brief dropRejected 时为拒绝原因。 */
    std::string reason;
};

/** @brief 手牌：扇形布局 + 悬浮 + 拖拽 + 落区判定 + 渲染。 */
class EVENGINE_API_WORLD Hand : public ecs::Entity {
public:
    /** @brief Entity. */
    ENTITY(Hand, ecs::Entity)

    /** @brief Release. */
    void release() override { ecs::DestroyEntity(this); }

    /** @brief 所有者与布局/交互开关。 */
    struct Meta {
        std::string owner = "player";
        LayoutConfig *config = nullptr;
        bool faceDown = false;
        bool peek = false;
        bool interactive = true;
    };
    /** @brief 有序手牌列表。 */
    struct Membership {
        std::vector<CardData *> cards;
    };
    /** @brief 拖拽内部状态（勿直接修改）。 */
    struct Drag {
        CardData *card = nullptr;
        CardData *press = nullptr;
        float ox = 0.f, oy = 0.f;
        float pressX = 0.f, pressY = 0.f;
        bool wasDown = false;
    };

    COMPONENT(Meta, meta)
    COMPONENT(Membership, membership)
    COMPONENT(Drag, drag)

    /** @brief 创建并触摸全部组件。 */
    static Hand *createHand();

    /** @brief 手牌增删与查询。 */
    void addCard(CardData *c);
    bool removeCard(CardData *c);
    void clear();
    int  count() { return static_cast<int>(membership()->cards.size()); }
    CardData *get(int index);
    CardData *find(const std::string &id);
    /** @brief 命中检测：返回 (px,py) 处最上层卡牌。 */
    CardData *pick(float px, float py);

    /** @brief 第 i 张（共 n 张）的扇形目标位置（牌中心）。 */
    void slotTransform(int i, int n, float &ox, float &oy, float &angle);

    /** @brief 每帧推进：布局 + 悬浮 + 拖拽状态机，产出事件到 out。 */
    void update(float dt, float mx, float my, bool down, const std::vector<Zone *> &zones,
                std::vector<CardEvent> &out);

    /** @brief 绘制整手牌。 */
    void render(graphics::Graphics *gfx);
};

/**
 * @brief Evaluated flip pose for external 2D/3D adapters (graphics-independent).
 *
 * Progress 0 is face-up and 1 is face-down. showingFront swaps at the edge-on
 * pinch where axisScale reaches 0 so adapters never show the new face through the old one.
 */
struct CardFlipPose {
    float progress     = 0.f;
    float angleRadians = 0.f;
    float angleDegrees = 0.f;
    float lift         = 0.f;
    float axisScale    = 1.f;
    bool  showingFront = true;
};

/** @brief Evaluate face-down progress into an adapter-facing flip pose.
 * @param flipProgress Animated face-down amount in [0, 1].
 * @param liftPeak Peak rise at the edge-on midpoint (same units as layout).
 */
EVENGINE_API_WORLD CardFlipPose evaluateCardFlip(float flipProgress, float liftPeak);

/** @brief Advance animated flip toward a face-down target with frame-rate-normalized speed. */
EVENGINE_API_WORLD void advanceCardFlip(float &flip, float targetFaceDown, float speed, float dt);

/**
 * @brief Resolve whether a card should read as face-up under hand faceDown/peek rules.
 * @param cardFaceUp Per-card logical face target.
 * @param handFaceDown When true, the owning hand forces back-face display unless peeking.
 * @param handPeek When the hand is face-down, peek reveals the front.
 */
EVENGINE_API_WORLD bool resolveCardWantFaceUp(bool cardFaceUp, bool handFaceDown, bool handPeek);

/** @brief 渲染辅助（也供 Zone / Deck 复用）。 */
void printCentered(graphics::Graphics *gfx, const std::string &text, float cx, float cy, float scale,
                   const glm::vec4 &color);
/** @brief 绘制牌背。 */
void renderCardBack(graphics::Graphics *gfx, float x, float y, float w, float h, float angle, float a);
/** @brief 绘制单张卡牌（正面或牌背；back 强制牌背，忽略 flip）。 */
void renderCard(graphics::Graphics *gfx, const CardData &card, const LayoutConfig &cfg, bool back);

}  // namespace eve::card
