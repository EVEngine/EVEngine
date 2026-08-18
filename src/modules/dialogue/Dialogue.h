#pragma once

#include "avatar/AvatarInstance.h"
#include "common/Module.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::dialogue {

/**
 * @brief Visual-novel style dialogue stage.
 * Script: `dlg <- eve.Dialogue();`
 *
 * Dialogue scripts remain Squirrel (functions / generators). This module only
 * owns speaker lines, typewriter, choices, and avatar stage slots.
 */
class Dialogue : public Module {
public:
    Module_REG(Dialogue);
    Dialogue() = default;
    ~Dialogue() override = default;

    /** @brief 角色：注册 / 查询 / 绑定 Avatar。 */
    bool registerCharacter(const std::string &id, const std::string &displayName);
    bool hasCharacter(const std::string &id) const;
    std::string getDisplayName(const std::string &id) const;
    bool bindAvatar(const std::string &id, avatar::AvatarInstance *av);
    avatar::AvatarInstance *getAvatar(const std::string &id) const;
    int getCharacterCount() const;
    std::string getCharacterId(int index) const;

    /** @brief 舞台：显示/隐藏角色、槽位与表情/动作。 */
    bool show(const std::string &id, const std::string &slot);
    bool hide(const std::string &id);
    bool isShown(const std::string &id) const;
    std::string getSlot(const std::string &id) const;
    void setSlotX(const std::string &slot, float xNorm);
    float getSlotX(const std::string &slot) const;
    bool setExpression(const std::string &id, const std::string &expression);
    bool setMotion(const std::string &id, const std::string &motion);
    /** Place visible avatars using normalized slot X * stageWidth. */
    void syncStage(float stageWidth, float stageHeight);

    /** @brief 台词：说话/旁白、打字机效果与推进。 */
    void say(const std::string &speakerId, const std::string &text);
    void narrate(const std::string &text);
    void setTypeSpeed(float charsPerSecond);
    float getTypeSpeed() const { return typeSpeed_; }
    void skipTyping();
    bool isTyping() const;
    bool isWaitingAdvance() const;
    bool isIdle() const;
    void advance();

    std::string getSpeakerId() const { return speakerId_; }
    std::string getSpeakerName() const;
    std::string getFullText() const { return fullText_; }
    std::string getVisibleText() const;
    std::string getPhase() const;

    /** @brief 口型同步：打字时驱动说话者 Avatar 参数。 */
    void setLipSyncEnabled(bool enabled);
    bool isLipSyncEnabled() const { return lipSyncEnabled_; }
    void setLipSyncParameter(const std::string &name);
    std::string getLipSyncParameter() const { return lipSyncParameter_; }
    void setLipSyncAmplitude(float amplitude);
    float getLipSyncAmplitude() const { return lipSyncAmplitude_; }
    float getLipSyncValue() const { return lipSyncValue_; }

    /** @brief 选项：清空/添加/展示与选择。 */
    void clearChoices();
    bool addChoice(const std::string &id, const std::string &label);
    void presentChoices();
    bool isWaitingChoice() const;
    int getChoiceCount() const;
    std::string getChoiceId(int index) const;
    std::string getChoiceLabel(int index) const;
    bool selectChoice(int index);
    std::string getSelectedChoiceId() const { return selectedChoiceId_; }

    /** @brief 推进打字机 / 口型同步 / 阶段机；每帧调用。 */
    void update(float dt);
    /** @brief 重置舞台与台词状态。 */
    void reset();

private:
    struct Character {
        std::string id;
        std::string displayName;
        avatar::AvatarInstance *avatar = nullptr;
        std::string slot;
        bool shown = false;
    };

    struct Choice {
        std::string id;
        std::string label;
    };

    enum class Phase { Idle, Typing, WaitingAdvance, WaitingChoice };

    Character *findCharacter(const std::string &id);
    const Character *findCharacter(const std::string &id) const;
    void beginLine(const std::string &speakerId, const std::string &text);

    std::vector<Character> characters_;
    std::unordered_map<std::string, float> slotX_;  // normalized 0..1

    Phase phase_ = Phase::Idle;
    std::string speakerId_;
    std::string fullText_;
    float typeSpeed_ = 40.f;  // chars / second
    float typed_ = 0.f;

    std::vector<Choice> choices_;
    std::string selectedChoiceId_;

    bool lipSyncEnabled_ = true;
    std::string lipSyncParameter_ = "mouthOpen";
    float lipSyncAmplitude_ = 0.85f;
    float lipSyncValue_ = 0.f;
    float lipSyncTime_ = 0.f;

    void updateLipSync(float dt);
    void applyLipSyncToSpeaker();
};

}  // namespace eve::dialogue
