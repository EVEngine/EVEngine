#pragma once

/**
 * @brief 卡牌游戏 UI 工具模块：工厂 + 脚本绑定入口。
 * 功能参考 ycarowr/UiCard：扇形手牌布局、抽牌/洗牌、悬浮放大、拖拽到落牌区、
 * 敌方手牌（背面/偷看）、费用不足置灰，以及可实时调节的布局参数。
 * 数据模型与渲染/交互逻辑见 CardTypes.h。
 */

#include "common/ECS.h"
#include "common/Module.h"
#include "card/CardTypes.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace eve::graphics {
class Graphics;
}

namespace eve::card {

/** @brief Immutable presentation data captured from one card for external 2D/3D adapters. */
struct CardPresentationSnapshot {
    std::string instanceId;
    std::string definitionId;
    std::string state;
    float x = 0.f;
    float y = 0.f;
    float width = 0.f;
    float height = 0.f;
    float angle = 0.f;
    float scale = 1.f;
    float alpha = 1.f;
    bool hovered = false;
    bool dragging = false;
    bool disabled = false;
    bool faceUp = true;
};

/** @brief Maps a logical card canvas to a 3D parallelogram without depending on graphics. */
class CardPlaneMapper {
public:
    /** @brief Configure the logical rectangle represented by the world plane. */
    void setLogicalRect(float x, float y, float width, float height);
    /** @brief Configure world origin plus full-width U and full-height V vectors. */
    void setPlane(float ox, float oy, float oz, float ux, float uy, float uz,
                  float vx, float vy, float vz);
    /** @brief Intersect a world ray and store its logical and world hit coordinates. */
    bool mapRay(float ox, float oy, float oz, float dx, float dy, float dz);
    /** @brief Map a logical point to the configured world plane. */
    bool mapLayout(float x, float y);
    /** @brief Return the most recently mapped logical X coordinate. */
    float getLogicalX() const { return resultLogicalX_; }
    /** @brief Return the most recently mapped logical Y coordinate. */
    float getLogicalY() const { return resultLogicalY_; }
    /** @brief Return the most recently mapped world X coordinate. */
    float getWorldX() const { return resultWorld_.x; }
    /** @brief Return the most recently mapped world Y coordinate. */
    float getWorldY() const { return resultWorld_.y; }
    /** @brief Return the most recently mapped world Z coordinate. */
    float getWorldZ() const { return resultWorld_.z; }

private:
    glm::vec3 origin_{0.f};
    glm::vec3 axisU_{1.f, 0.f, 0.f};
    glm::vec3 axisV_{0.f, 0.f, 1.f};
    glm::vec3 resultWorld_{0.f};
    float logicalX_ = 0.f;
    float logicalY_ = 0.f;
    float logicalWidth_ = 1.f;
    float logicalHeight_ = 1.f;
    float resultLogicalX_ = 0.f;
    float resultLogicalY_ = 0.f;
};

/** @brief 卡牌模块入口（eve.Card）：定义注册、对象工厂与每帧 update/render。 */
class Card : public Module {
public:
    Module_REG(Card);
    Card() = default;
    ~Card() override;

    /** @brief 从 JSON 注册卡牌类型；返回成功注册数量。 */
    int registerCardsFromJson(const std::string &json);
    /** @brief 清空全部卡牌类型定义。 */
    void clearCardDefinitions();
    /** @brief 已注册卡牌类型数量。 */
    int getCardDefinitionCount();
    /** @brief 卡牌类型查询。 */
    bool hasCardDefinition(const std::string &id);
    std::string getCardDefinitionName(const std::string &id);
    std::string getCardDefinitionKind(const std::string &id);
    int getCardDefinitionCost(const std::string &id);
    int getCardDefinitionAttack(const std::string &id);
    int getCardDefinitionHealth(const std::string &id);
    float getCardDefinitionTintR(const std::string &id);
    float getCardDefinitionTintG(const std::string &id);
    float getCardDefinitionTintB(const std::string &id);

    /** @brief 工厂：对象由 ECS 表持有，脚本持有的是非拥有句柄。 */
    LayoutConfig *newConfig();
    CardData *newCard(const std::string &defId);
    Deck *newDeck();
    /** @brief 创建一个落牌区。 */
    Zone *newZone(const std::string &id, const std::string &label, float x, float y, float w, float h);
    /** @brief 创建一个手牌布局。 */
    Hand *newHand(LayoutConfig *cfg);
    /** @brief Create a reusable logical-card-canvas to 3D plane mapper. */
    CardPlaneMapper *newPlaneMapper();

    /** @brief 游戏状态：当前布局、手牌、落牌区、牌库。 */
    void setConfig(LayoutConfig *cfg);
    LayoutConfig *getConfig() const;
    int handCount() const;
    Hand *getHand(int index) const;
    Hand *findHand(const std::string &owner) const;
    int zoneCount() const;
    Zone *getZone(int index) const;
    Deck *getDeck() const;
    /** @brief 从牌库抽一张牌加入指定手牌；无牌时返回 nullptr。 */
    CardData *drawCard(const std::string &handOwner);

    /** @brief Capture all live cards into a contiguous presentation snapshot buffer. */
    int capturePresentation();
    /** @brief Return the number of entries in the most recently captured buffer. */
    int getPresentationCount() const;
    /** @brief Return an entry from the most recently captured buffer, or nullptr. */
    CardPresentationSnapshot *getPresentation(int index);

    /** @brief 每帧更新布局与交互。 */
    void update(float dt, float mx, float my, bool down);
    /** @brief 绘制手牌/落牌区（与牌库）。 */
    void render(graphics::Graphics *gfx);
    void renderDeck(graphics::Graphics *gfx);

    /** @brief Enable or disable the built-in card visuals while retaining layout and interaction. */
    void setBuiltInVisuals(bool enabled) { builtInVisuals_ = enabled; }
    /** @brief Return whether Card::render draws the built-in card faces. */
    bool getBuiltInVisuals() const { return builtInVisuals_; }

    /** @brief Begin a drag-to-target gesture at the supplied canvas position. */
    void beginTargeting(const std::string &sourceId, float x, float y);
    /** @brief Update the pointer and current game-validated target. */
    void updateTargeting(float x, float y, const std::string &targetId, bool valid);
    /** @brief Cancel the active target gesture. */
    void cancelTargeting();
    /** @brief Draw the current target arrow; target legality remains game-defined. */
    void renderTargeting(graphics::Graphics *gfx);
    /** @brief Return whether a target gesture is active. */
    bool isTargeting() const { return targetingActive_; }
    /** @brief Return the game-supplied legality of the current target. */
    bool isTargetValid() const { return targetingValid_; }
    /** @brief Return the source card instance ID for the active gesture. */
    std::string getTargetSource() const { return targetingSource_; }
    /** @brief Return the current game-supplied target ID, or an empty string. */
    std::string getTargetId() const { return targetingId_; }

    /** @brief 交互事件队列（抽牌/出牌/拖拽等）。 */
    void clearEvents();
    int getEventCount() const;
    std::string getEventType(int index) const;
    std::string getEventHand(int index) const;
    std::string getEventZone(int index) const;
    std::string getEventCardId(int index) const;
    std::string getEventReason(int index) const;

private:
    const CardDefinition *findDef(const std::string &id) const;

    std::unordered_map<std::string, CardDefinition> defs_;
    std::vector<std::unique_ptr<LayoutConfig>> configs_;
    std::vector<std::unique_ptr<CardPlaneMapper>> planeMappers_;
    std::vector<CardEvent> events_;
    std::vector<ecs::EntityHandle> cards_;
    std::vector<ecs::EntityHandle> decks_;
    std::vector<ecs::EntityHandle> zones_;
    std::vector<ecs::EntityHandle> hands_;
    std::vector<CardPresentationSnapshot> presentation_;

    LayoutConfig *activeConfig_ = nullptr;
    Deck *activeDeck_ = nullptr;
    int nextInstance_ = 1;
    bool builtInVisuals_ = true;
    bool targetingActive_ = false;
    bool targetingValid_ = false;
    float targetingStartX_ = 0.f;
    float targetingStartY_ = 0.f;
    float targetingX_ = 0.f;
    float targetingY_ = 0.f;
    std::string targetingSource_;
    std::string targetingId_;
};

}  // namespace eve::card
