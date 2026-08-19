#pragma once

/**
 * @brief 卡牌游戏 UI 工具模块：数据模型与渲染/交互逻辑（功能参考 ycarowr/UiCard）。
 * 模块入口 eve.Card() 见 Card.h；本文件是 CardData/Deck/Zone/Hand 等对象与布局渲染实现。
 */

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
const char *cardStateName(CardState state);

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
};

/** @brief 单张卡牌：数据 + 运行时布局状态。 */
class CardData {
public:
    std::string      id;
    std::string      name;
    std::string      kind = "creature";
    int              cost = 0;
    int              attack = 0;
    int              health = 0;
    /** @brief 是否正面朝上（false 渲染为牌背）。 */
    bool             faceUp = true;
    /** @brief 禁用：置灰且不可拖拽。 */
    bool             disabled = false;
    CardState        state = CardState::Deck;
    glm::vec3        tint{0.62f, 0.50f, 0.40f};
    /** @brief 可选卡图。 */
    graphics::Texture *texture = nullptr;

    /** @brief 运行时布局（由 Hand 维护）：中心坐标。 */
    float x = 0.f;
    float y = 0.f;
    /** @brief 宽高/旋转/缩放/透明度。 */
    float w = 0.f;
    float h = 0.f;
    float angle = 0.f;
    float scale = 1.f;
    float alpha = 1.f;
    bool  hovered = false;
    bool  dragging = false;

    /** @brief 以当前中心 + 缩放判断点是否命中。 */
    bool hit(float px, float py) const;
    /** @brief 卡牌描述字符串（调试用）。 */
    std::string describe() const;
};

/** @brief 牌库（栈顶在末尾）。 */
class Deck {
public:
    std::vector<CardData *> cards;

    /** @brief 将卡牌压入栈顶（末尾）。 */
    void push(CardData *c) { cards.push_back(c); }
    /** @brief 弹出栈顶卡牌（末尾）；空牌库返回 nullptr。 */
    CardData *draw();
    /** @brief 查看栈顶卡牌（不弹出）。 */
    CardData *peek() const;
    /** @brief 清空牌库。 */
    void clear() { cards.clear(); }
    /** @brief 洗牌。 */
    void shuffle();
    /** @brief 牌库数量/是否为空/按下标取牌。 */
    int  count() const { return static_cast<int>(cards.size()); }
    bool isEmpty() const { return cards.empty(); }
    CardData *get(int index) const;
};

/** @brief 落牌区（手牌区 / 出牌区 / 弃牌区），用于拖放命中判定。 */
class Zone {
public:
    std::string id;
    std::string label;
    /** @brief 区域矩形（像素）。 */
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    /** @brief 区域底色。 */
    glm::vec3 color{0.30f, 0.60f, 0.30f};
    float alpha = 0.16f;
    bool  enabled = true;
    /** @brief 接受的卡牌 kind；空 = 接受任意 kind。 */
    std::vector<std::string> acceptKinds;

    /** @brief 点是否落在区域内。 */
    bool contains(float px, float py) const;
    /** @brief 该区域是否接受这张卡。 */
    bool accepts(const CardData *card) const;
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
class Hand {
public:
    std::string owner = "player";
    /** @brief 指向模块拥有的布局配置。 */
    LayoutConfig *config = nullptr;
    std::vector<CardData *> cards;
    /** @brief 整手渲染为牌背（敌方手牌）。 */
    bool faceDown = false;
    /** @brief 偷看：faceDown 时也渲染正面。 */
    bool peek = false;
    /** @brief 是否允许拖拽。 */
    bool interactive = true;

    /** @brief 拖拽内部状态（勿直接修改）。 */
    CardData *_dragCard = nullptr;
    CardData *_pressCard = nullptr;
    float _dragOx = 0.f, _dragOy = 0.f;
    float _pressX = 0.f, _pressY = 0.f;
    bool  _wasDown = false;

    /** @brief 手牌增删与查询。 */
    void addCard(CardData *c);
    bool removeCard(CardData *c);
    void clear();
    int  count() const { return static_cast<int>(cards.size()); }
    CardData *get(int index) const;
    CardData *find(const std::string &id) const;
    /** @brief 命中检测：返回 (px,py) 处最上层卡牌。 */
    CardData *pick(float px, float py) const;

    /** @brief 第 i 张（共 n 张）的扇形目标位置（牌中心）。 */
    void slotTransform(int i, int n, float &ox, float &oy, float &angle) const;

    /** @brief 每帧推进：布局 + 悬浮 + 拖拽状态机，产出事件到 out。 */
    void update(float dt, float mx, float my, bool down, const std::vector<Zone *> &zones,
                std::vector<CardEvent> &out);

    /** @brief 绘制整手牌。 */
    void render(graphics::Graphics *gfx);
};

/** @brief 渲染辅助（也供 Zone / Deck 复用）。 */
void printCentered(graphics::Graphics *gfx, const std::string &text, float cx, float cy, float scale,
                   const glm::vec4 &color);
/** @brief 绘制牌背。 */
void renderCardBack(graphics::Graphics *gfx, float x, float y, float w, float h, float angle, float a);
/** @brief 绘制单张卡牌（正面或牌背）。 */
void renderCard(graphics::Graphics *gfx, const CardData &card, const LayoutConfig &cfg, bool back);

}  // namespace eve::card
