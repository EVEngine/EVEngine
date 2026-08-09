#pragma once

#include "avatar/AvatarInstance.h"
#include "common/Module.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace eve::dialogue {

/**
 * Visual-novel style dialogue stage.
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

    // ---- characters ----
    bool registerCharacter(const std::string &id, const std::string &displayName);
    bool hasCharacter(const std::string &id) const;
    std::string getDisplayName(const std::string &id) const;
    bool bindAvatar(const std::string &id, avatar::AvatarInstance *av);
    avatar::AvatarInstance *getAvatar(const std::string &id) const;
    int getCharacterCount() const;
    std::string getCharacterId(int index) const;

    // ---- stage ----
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

    // ---- lines ----
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

    // ---- choices ----
    void clearChoices();
    bool addChoice(const std::string &id, const std::string &label);
    void presentChoices();
    bool isWaitingChoice() const;
    int getChoiceCount() const;
    std::string getChoiceId(int index) const;
    std::string getChoiceLabel(int index) const;
    bool selectChoice(int index);
    std::string getSelectedChoiceId() const { return selectedChoiceId_; }

    void update(float dt);
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
};

}  // namespace eve::dialogue
