#pragma once

// 卡牌游戏 UI 工具模块：数据模型与渲染/交互逻辑（功能参考 ycarowr/UiCard）。
// 模块入口 eve.Card() 见 Card.h；本文件是 CardData/Deck/Zone/Hand 等对象与布局渲染实现。

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

const char *cardStateName(CardState state);

// UiCard 风格的布局配置（脚本可实时修改，对应 UiCard 的 Configs 面板）。
struct LayoutConfig {
    float cardW = 110.f;        // 卡牌宽
    float cardH = 150.f;        // 卡牌高
    float spacing = 26.f;       // 手牌水平间距
    float handX = 640.f;        // 手牌扇形中心 X
    float handY = 640.f;        // 手牌基准高度 Y
    float arcHeight = 46.f;     // 扇形弧度（两端上翘高度）
    float rotationAngle = 14.f; // 手牌两端最大旋转角（度）
    bool  hoverRotation = true; // 悬浮时是否恢复为水平
    float hoverScale = 1.22f;   // 悬浮缩放
    float hoverLift = 46.f;     // 悬浮上移像素
    float hoverSpeed = 0.18f;   // 悬浮运动速度（每帧插值系数）
    float motionSpeed = 0.12f;  // 通用归位运动速度
    float disabledAlpha = 0.35f; // 禁用牌透明度
    bool  showZones = true;     // 是否绘制落牌区
    float deckX = 360.f;        // 牌库堆绘制位置
    float deckY = 640.f;
    float dragThreshold = 64.f; // 拖拽触发距离平方（像素）
};

// 卡牌类型定义（registerCardsFromJson 注册）。
struct CardDefinition {
    std::string id;
    std::string name;
    std::string kind = "creature";  // "creature" | "spell" | ...
    int         cost = 0;
    int         attack = 0;
    int         health = 0;
    glm::vec3   tint{0.62f, 0.50f, 0.40f};
};

// 单张卡牌：数据 + 运行时布局状态。
class CardData {
public:
    std::string      id;
    std::string      name;
    std::string      kind = "creature";
    int              cost = 0;
    int              attack = 0;
    int              health = 0;
    bool             faceUp = true;   // false 渲染为牌背
    bool             disabled = false; // 禁用：置灰且不可拖拽
    CardState        state = CardState::Deck;
    glm::vec3        tint{0.62f, 0.50f, 0.40f};
    graphics::Texture *texture = nullptr;  // 可选卡图

    // 运行时布局（由 Hand 维护）
    float x = 0.f;   // 中心 X
    float y = 0.f;   // 中心 Y
    float w = 0.f;
    float h = 0.f;
    float angle = 0.f;
    float scale = 1.f;
    float alpha = 1.f;
    bool  hovered = false;
    bool  dragging = false;

    // 以当前中心 + 缩放判断点是否命中
    bool hit(float px, float py) const;
    std::string describe() const;
};

// 牌库（栈顶在末尾）。
class Deck {
public:
    std::vector<CardData *> cards;

    void push(CardData *c) { cards.push_back(c); }
    CardData *draw();
    CardData *peek() const;
    void clear() { cards.clear(); }
    void shuffle();
    int  count() const { return static_cast<int>(cards.size()); }
    bool isEmpty() const { return cards.empty(); }
    CardData *get(int index) const;
};

// 落牌区（手牌区 / 出牌区 / 弃牌区），用于拖放命中判定。
class Zone {
public:
    std::string id;
    std::string label;
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
    glm::vec3 color{0.30f, 0.60f, 0.30f};
    float alpha = 0.16f;
    bool  enabled = true;
    std::vector<std::string> acceptKinds;  // 空 = 接受任意 kind

    bool contains(float px, float py) const;
    bool accepts(const CardData *card) const;
    void render(graphics::Graphics *gfx, bool showLabel);
};

// 一次交互事件。
struct CardEvent {
    std::string type;    // "click" | "drop"
    std::string hand;    // 来源手牌 owner
    std::string zoneId;  // drop 目标的落牌区 id（click 为空）
    std::string cardId;
    std::string reason;  // dropRejected 时为拒绝原因
};

// 手牌：扇形布局 + 悬浮 + 拖拽 + 落区判定 + 渲染。
class Hand {
public:
    std::string owner = "player";
    LayoutConfig *config = nullptr;  // 指向模块拥有的配置
    std::vector<CardData *> cards;
    bool faceDown = false;    // 整手渲染为牌背（敌方手牌）
    bool peek = false;        // 偷看：faceDown 时也渲染正面
    bool interactive = true;  // 是否允许拖拽

    // 拖拽内部状态
    CardData *_dragCard = nullptr;
    CardData *_pressCard = nullptr;
    float _dragOx = 0.f, _dragOy = 0.f;
    float _pressX = 0.f, _pressY = 0.f;
    bool  _wasDown = false;

    void addCard(CardData *c);
    bool removeCard(CardData *c);
    void clear();
    int  count() const { return static_cast<int>(cards.size()); }
    CardData *get(int index) const;
    CardData *find(const std::string &id) const;
    CardData *pick(float px, float py) const;

    // 扇形目标位置（牌中心）
    void slotTransform(int i, int n, float &ox, float &oy, float &angle) const;

    // 每帧推进：布局 + 悬浮 + 拖拽状态机，产出事件到 out。
    void update(float dt, float mx, float my, bool down, const std::vector<Zone *> &zones,
                std::vector<CardEvent> &out);

    void render(graphics::Graphics *gfx);
};

// 渲染辅助（也供 Zone / Deck 复用）。
void printCentered(graphics::Graphics *gfx, const std::string &text, float cx, float cy, float scale,
                   const glm::vec4 &color);
void renderCardBack(graphics::Graphics *gfx, float x, float y, float w, float h, float angle, float a);
void renderCard(graphics::Graphics *gfx, const CardData &card, const LayoutConfig &cfg, bool back);

}  // namespace eve::card
