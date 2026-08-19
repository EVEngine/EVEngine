#pragma once

/**
 * @brief 卡牌游戏 UI 工具模块：工厂 + 脚本绑定入口。
 * 功能参考 ycarowr/UiCard：扇形手牌布局、抽牌/洗牌、悬浮放大、拖拽到落牌区、
 * 敌方手牌（背面/偷看）、费用不足置灰，以及可实时调节的布局参数。
 * 数据模型与渲染/交互逻辑见 CardTypes.h。
 */

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

    /** @brief 工厂：对象由模块持有，脚本持有的是非拥有句柄。 */
    LayoutConfig *newConfig();
    CardData *newCard(const std::string &defId);
    Deck *newDeck();
    /** @brief 创建一个落牌区。 */
    Zone *newZone(const std::string &id, const std::string &label, float x, float y, float w, float h);
    /** @brief 创建一个手牌布局。 */
    Hand *newHand(LayoutConfig *cfg);

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

    /** @brief 每帧更新布局与交互。 */
    void update(float dt, float mx, float my, bool down);
    /** @brief 绘制手牌/落牌区（与牌库）。 */
    void render(graphics::Graphics *gfx);
    void renderDeck(graphics::Graphics *gfx);

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
    std::vector<std::unique_ptr<CardData>> cards_;
    std::vector<std::unique_ptr<Deck>> decks_;
    std::vector<std::unique_ptr<Zone>> zones_;
    std::vector<std::unique_ptr<Hand>> hands_;
    std::vector<CardEvent> events_;

    LayoutConfig *activeConfig_ = nullptr;
    Deck *activeDeck_ = nullptr;
    int nextInstance_ = 1;
};

}  // namespace eve::card
