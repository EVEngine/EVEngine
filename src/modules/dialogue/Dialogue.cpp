#include "dialogue/Dialogue.h"

#include <algorithm>
#include <cmath>
#include <simplesquirrel/simplesquirrel.hpp>

namespace {

size_t utf8CodepointCount(const std::string &s) {
    size_t n = 0;
    for (size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t step = 1;
        if ((c & 0x80) == 0)
            step = 1;
        else if ((c & 0xE0) == 0xC0)
            step = 2;
        else if ((c & 0xF0) == 0xE0)
            step = 3;
        else if ((c & 0xF8) == 0xF0)
            step = 4;
        if (i + step > s.size()) break;
        i += step;
        ++n;
    }
    return n;
}

size_t utf8ByteOffsetForCodepoints(const std::string &s, size_t codepoints) {
    size_t n = 0;
    size_t i = 0;
    while (i < s.size() && n < codepoints) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        size_t step = 1;
        if ((c & 0x80) == 0)
            step = 1;
        else if ((c & 0xE0) == 0xC0)
            step = 2;
        else if ((c & 0xF0) == 0xE0)
            step = 3;
        else if ((c & 0xF8) == 0xF0)
            step = 4;
        if (i + step > s.size()) break;
        i += step;
        ++n;
    }
    return i;
}

}  // namespace

namespace eve::dialogue {

Module_IMPL(Dialogue, new Dialogue());

Dialogue::Character *Dialogue::findCharacter(const std::string &id) {
    for (auto &c : characters_)
        if (c.id == id) return &c;
    return nullptr;
}

const Dialogue::Character *Dialogue::findCharacter(const std::string &id) const {
    for (const auto &c : characters_)
        if (c.id == id) return &c;
    return nullptr;
}

bool Dialogue::registerCharacter(const std::string &id, const std::string &displayName) {
    if (id.empty()) return false;
    if (auto *c = findCharacter(id)) {
        c->displayName = displayName.empty() ? id : displayName;
        return true;
    }
    Character c;
    c.id = id;
    c.displayName = displayName.empty() ? id : displayName;
    characters_.push_back(c);
    return true;
}

bool Dialogue::hasCharacter(const std::string &id) const { return findCharacter(id) != nullptr; }

std::string Dialogue::getDisplayName(const std::string &id) const {
    const Character *c = findCharacter(id);
    return c ? c->displayName : std::string{};
}

bool Dialogue::bindAvatar(const std::string &id, avatar::AvatarInstance *av) {
    Character *c = findCharacter(id);
    if (!c) return false;
    c->avatar = av;
    return true;
}

avatar::AvatarInstance *Dialogue::getAvatar(const std::string &id) const {
    const Character *c = findCharacter(id);
    return c ? c->avatar : nullptr;
}

int Dialogue::getCharacterCount() const { return int(characters_.size()); }

std::string Dialogue::getCharacterId(int index) const {
    if (index < 0 || size_t(index) >= characters_.size()) return {};
    return characters_[size_t(index)].id;
}

bool Dialogue::show(const std::string &id, const std::string &slot) {
    Character *c = findCharacter(id);
    if (!c) return false;
    c->shown = true;
    c->slot = slot.empty() ? "center" : slot;
    if (c->avatar) c->avatar->setVisible(true);
    return true;
}

bool Dialogue::hide(const std::string &id) {
    Character *c = findCharacter(id);
    if (!c) return false;
    c->shown = false;
    if (c->avatar) c->avatar->setVisible(false);
    return true;
}

bool Dialogue::isShown(const std::string &id) const {
    const Character *c = findCharacter(id);
    return c && c->shown;
}

std::string Dialogue::getSlot(const std::string &id) const {
    const Character *c = findCharacter(id);
    return c ? c->slot : std::string{};
}

void Dialogue::setSlotX(const std::string &slot, float xNorm) {
    if (slot.empty()) return;
    slotX_[slot] = xNorm;
}

float Dialogue::getSlotX(const std::string &slot) const {
    auto it = slotX_.find(slot);
    if (it != slotX_.end()) return it->second;
    if (slot == "left") return 0.25f;
    if (slot == "right") return 0.75f;
    if (slot == "center") return 0.5f;
    return 0.5f;
}

bool Dialogue::setExpression(const std::string &id, const std::string &expression) {
    Character *c = findCharacter(id);
    if (!c || !c->avatar) return false;
    c->avatar->setExpression(expression);
    return true;
}

bool Dialogue::setMotion(const std::string &id, const std::string &motion) {
    Character *c = findCharacter(id);
    if (!c || !c->avatar) return false;
    c->avatar->setMotion(motion);
    return true;
}

void Dialogue::syncStage(float stageWidth, float stageHeight) {
    (void)stageHeight;
    for (Character &c : characters_) {
        if (!c.avatar) continue;
        c.avatar->setVisible(c.shown);
        if (!c.shown) {
            c.avatar->sync();
            continue;
        }
        const float xn = getSlotX(c.slot);
        const float x = xn * stageWidth;
        // Keep current Y; only drive X from slot.
        c.avatar->setPosition(x, c.avatar->getY());
        c.avatar->sync();
    }
}

void Dialogue::beginLine(const std::string &speakerId, const std::string &text) {
    speakerId_ = speakerId;
    fullText_ = text;
    typed_ = 0.f;
    selectedChoiceId_.clear();
    lipSyncTime_ = 0.f;
    if (typeSpeed_ <= 0.f) {
        typed_ = float(utf8CodepointCount(fullText_));
        phase_ = Phase::WaitingAdvance;
        lipSyncValue_ = 0.f;
    } else {
        phase_ = Phase::Typing;
    }
}

void Dialogue::say(const std::string &speakerId, const std::string &text) {
    beginLine(speakerId, text);
}

void Dialogue::narrate(const std::string &text) { beginLine("", text); }

void Dialogue::setTypeSpeed(float charsPerSecond) { typeSpeed_ = charsPerSecond; }

void Dialogue::skipTyping() {
    if (phase_ == Phase::Typing) {
        typed_ = float(utf8CodepointCount(fullText_));
        phase_ = Phase::WaitingAdvance;
    }
}

bool Dialogue::isTyping() const { return phase_ == Phase::Typing; }

bool Dialogue::isWaitingAdvance() const { return phase_ == Phase::WaitingAdvance; }

bool Dialogue::isIdle() const { return phase_ == Phase::Idle; }

void Dialogue::advance() {
    if (phase_ == Phase::Typing) {
        skipTyping();
        return;
    }
    if (phase_ == Phase::WaitingAdvance) phase_ = Phase::Idle;
}

std::string Dialogue::getSpeakerName() const {
    if (speakerId_.empty()) return {};
    return getDisplayName(speakerId_);
}

std::string Dialogue::getVisibleText() const {
    if (fullText_.empty()) return {};
    const size_t total = utf8CodepointCount(fullText_);
    size_t n = size_t(std::floor(typed_ + 1e-4f));
    if (n == 0) return {};
    if (n >= total) return fullText_;
    return fullText_.substr(0, utf8ByteOffsetForCodepoints(fullText_, n));
}

std::string Dialogue::getPhase() const {
    switch (phase_) {
        case Phase::Idle:
            return "idle";
        case Phase::Typing:
            return "typing";
        case Phase::WaitingAdvance:
            return "waiting_advance";
        case Phase::WaitingChoice:
            return "waiting_choice";
    }
    return "idle";
}

void Dialogue::clearChoices() {
    choices_.clear();
    selectedChoiceId_.clear();
}

bool Dialogue::addChoice(const std::string &id, const std::string &label) {
    if (id.empty()) return false;
    for (auto &ch : choices_) {
        if (ch.id == id) {
            ch.label = label;
            return true;
        }
    }
    choices_.push_back(Choice{id, label});
    return true;
}

void Dialogue::presentChoices() {
    if (choices_.empty()) {
        phase_ = Phase::Idle;
        return;
    }
    if (phase_ == Phase::Typing) skipTyping();
    phase_ = Phase::WaitingChoice;
}

bool Dialogue::isWaitingChoice() const { return phase_ == Phase::WaitingChoice; }

int Dialogue::getChoiceCount() const { return int(choices_.size()); }

std::string Dialogue::getChoiceId(int index) const {
    if (index < 0 || size_t(index) >= choices_.size()) return {};
    return choices_[size_t(index)].id;
}

std::string Dialogue::getChoiceLabel(int index) const {
    if (index < 0 || size_t(index) >= choices_.size()) return {};
    return choices_[size_t(index)].label;
}

bool Dialogue::selectChoice(int index) {
    if (phase_ != Phase::WaitingChoice) return false;
    if (index < 0 || size_t(index) >= choices_.size()) return false;
    selectedChoiceId_ = choices_[size_t(index)].id;
    phase_ = Phase::Idle;
    return true;
}

void Dialogue::setLipSyncEnabled(bool enabled) { lipSyncEnabled_ = enabled; }

void Dialogue::setLipSyncParameter(const std::string &name) {
    if (!name.empty()) lipSyncParameter_ = name;
}

void Dialogue::setLipSyncAmplitude(float amplitude) {
    if (amplitude < 0.f) amplitude = 0.f;
    if (amplitude > 2.f) amplitude = 2.f;
    lipSyncAmplitude_ = amplitude;
}

void Dialogue::updateLipSync(float dt) {
    if (dt < 0.f) dt = 0.f;
    if (lipSyncEnabled_ && phase_ == Phase::Typing && !speakerId_.empty()) {
        lipSyncTime_ += dt;
        // Simple mouth envelope while characters appear (no audio dependency).
        const float wave = std::fabs(std::sin(lipSyncTime_ * 14.f));
        lipSyncValue_ = lipSyncAmplitude_ * (0.25f + 0.75f * wave);
    } else {
        // Ease shut when not typing.
        lipSyncValue_ *= std::max(0.f, 1.f - dt * 8.f);
        if (lipSyncValue_ < 0.01f) lipSyncValue_ = 0.f;
    }
    applyLipSyncToSpeaker();
}

void Dialogue::applyLipSyncToSpeaker() {
    if (!lipSyncEnabled_ || speakerId_.empty() || lipSyncParameter_.empty()) return;
    Character *c = findCharacter(speakerId_);
    if (!c || !c->avatar) return;
    c->avatar->setParameter(lipSyncParameter_, lipSyncValue_);
}

void Dialogue::update(float dt) {
    if (dt < 0.f) dt = 0.f;
    if (phase_ == Phase::Typing) {
        const float total = float(utf8CodepointCount(fullText_));
        typed_ += typeSpeed_ * dt;
        if (typed_ >= total) {
            typed_ = total;
            phase_ = Phase::WaitingAdvance;
        }
    }
    updateLipSync(dt);
}

void Dialogue::reset() {
    phase_ = Phase::Idle;
    speakerId_.clear();
    fullText_.clear();
    typed_ = 0.f;
    choices_.clear();
    selectedChoiceId_.clear();
    lipSyncValue_ = 0.f;
    lipSyncTime_ = 0.f;
    for (Character &c : characters_) {
        c.shown = false;
        if (c.avatar) c.avatar->setVisible(false);
    }
}

void Dialogue::expose(ssq::Table &table) {
    auto cls = table.addClass(name, Dialogue::create, false);
    expose(cls);
}

void Dialogue::expose(ssq::Class &cls) {
    cls.addFunc("getName", &Dialogue::getName);
    cls.addFunc("registerCharacter", &Dialogue::registerCharacter);
    cls.addFunc("hasCharacter", &Dialogue::hasCharacter);
    cls.addFunc("getDisplayName", &Dialogue::getDisplayName);
    cls.addFunc("bindAvatar", &Dialogue::bindAvatar);
    cls.addFunc("getAvatar", &Dialogue::getAvatar);
    cls.addFunc("getCharacterCount", &Dialogue::getCharacterCount);
    cls.addFunc("getCharacterId", &Dialogue::getCharacterId);

    cls.addFunc("show", &Dialogue::show);
    cls.addFunc("hide", &Dialogue::hide);
    cls.addFunc("isShown", &Dialogue::isShown);
    cls.addFunc("getSlot", &Dialogue::getSlot);
    cls.addFunc("setSlotX", &Dialogue::setSlotX);
    cls.addFunc("getSlotX", &Dialogue::getSlotX);
    cls.addFunc("setExpression", &Dialogue::setExpression);
    cls.addFunc("setMotion", &Dialogue::setMotion);
    cls.addFunc("syncStage", &Dialogue::syncStage);

    cls.addFunc("say", &Dialogue::say);
    cls.addFunc("narrate", &Dialogue::narrate);
    cls.addFunc("setTypeSpeed", &Dialogue::setTypeSpeed);
    cls.addFunc("getTypeSpeed", &Dialogue::getTypeSpeed);
    cls.addFunc("skipTyping", &Dialogue::skipTyping);
    cls.addFunc("isTyping", &Dialogue::isTyping);
    cls.addFunc("isWaitingAdvance", &Dialogue::isWaitingAdvance);
    cls.addFunc("isIdle", &Dialogue::isIdle);
    cls.addFunc("advance", &Dialogue::advance);
    cls.addFunc("getSpeakerId", &Dialogue::getSpeakerId);
    cls.addFunc("getSpeakerName", &Dialogue::getSpeakerName);
    cls.addFunc("getFullText", &Dialogue::getFullText);
    cls.addFunc("getVisibleText", &Dialogue::getVisibleText);
    cls.addFunc("getPhase", &Dialogue::getPhase);

    cls.addFunc("setLipSyncEnabled", &Dialogue::setLipSyncEnabled);
    cls.addFunc("isLipSyncEnabled", &Dialogue::isLipSyncEnabled);
    cls.addFunc("setLipSyncParameter", &Dialogue::setLipSyncParameter);
    cls.addFunc("getLipSyncParameter", &Dialogue::getLipSyncParameter);
    cls.addFunc("setLipSyncAmplitude", &Dialogue::setLipSyncAmplitude);
    cls.addFunc("getLipSyncAmplitude", &Dialogue::getLipSyncAmplitude);
    cls.addFunc("getLipSyncValue", &Dialogue::getLipSyncValue);

    cls.addFunc("clearChoices", &Dialogue::clearChoices);
    cls.addFunc("addChoice", &Dialogue::addChoice);
    cls.addFunc("presentChoices", &Dialogue::presentChoices);
    cls.addFunc("isWaitingChoice", &Dialogue::isWaitingChoice);
    cls.addFunc("getChoiceCount", &Dialogue::getChoiceCount);
    cls.addFunc("getChoiceId", &Dialogue::getChoiceId);
    cls.addFunc("getChoiceLabel", &Dialogue::getChoiceLabel);
    cls.addFunc("selectChoice", &Dialogue::selectChoice);
    cls.addFunc("getSelectedChoiceId", &Dialogue::getSelectedChoiceId);

    cls.addFunc("update", &Dialogue::update);
    cls.addFunc("reset", &Dialogue::reset);
}

}  // namespace eve::dialogue
