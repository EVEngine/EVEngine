#include "card/CardState.h"

#include "card/CardTypes.h"
#include "common/Capability.h"
#include "common/ECS.h"
#include "common/IStateProvider.h"

#include <string>
#include <utility>

namespace eve::card {
namespace {

CardState parseCardState(const std::string& name) {
    if (name == "deck") return CardState::Deck;
    if (name == "hand") return CardState::Hand;
    if (name == "played") return CardState::Played;
    if (name == "discarded") return CardState::Discarded;
    if (name == "disabled") return CardState::Disabled;
    if (name == "returning") return CardState::Returning;
    return CardState::Hand;  // transient Hovered/Dragging normalize to Hand
}

/** @brief IStateProvider over live CardData/Hand entities. */
class CardStateProvider : public eve::caps::IStateProvider {
public:
    const char* stateKind() const override { return "card"; }

    bool captureState(StateValue& out) override { return captureCardState(out); }
    bool restoreState(const StateValue& in, std::string* err) override { return restoreCardState(in, err); }
    bool resetToDefaults() override { return resetCardState(); }
};

struct Register {
    Register() {
        static CardStateProvider provider;
        eve::cap::addListener<eve::caps::IStateProvider>(&provider);
    }
} g_register;

}  // namespace

bool captureCardState(StateValue& out) {
    StateValue cards = StateValue::array();
    auto       view  = ecs::View<CardData, CardData::Identity, CardData::State>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [id, st]   = *it;
        StateValue item = StateValue::object();
        item.set("id", StateValue::string(id->id));
        item.set("phase", StateValue::string(cardStateName(st->phase)));
        cards.pushBack(std::move(item));
    }
    out = StateValue::object();
    out.set("cards", std::move(cards));
    return true;
}

bool restoreCardState(const StateValue& in, std::string* err) {
    if (!in.isObject()) {
        if (err) *err = "card: state is not an object";
        return false;
    }
    const StateValue* cards = in.find("cards");
    if (!cards || !cards->isArray()) {
        if (err) *err = "card: missing cards";
        return false;
    }

    auto view = ecs::View<CardData, CardData::Identity, CardData::State>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [id, st] = *it;
        st->hovered   = false;
        st->dragging  = false;
        for (size_t i = 0; i < cards->arraySize(); ++i) {
            const StateValue& item  = cards->at(i);
            const StateValue* cid   = item.find("id");
            const StateValue* phase = item.find("phase");
            if (cid && phase && cid->isString() && phase->isString() && cid->asString() == id->id) {
                st->phase = parseCardState(phase->asString());
                break;
            }
        }
    }

    // In-flight drag is transient: dropped on reload.
    auto hands = ecs::View<Hand, Hand::Drag>();
    for (auto it = hands.begin(); it != hands.end(); ++it) {
        auto [drag]   = *it;
        drag->card    = nullptr;
        drag->press   = nullptr;
        drag->wasDown = false;
    }
    return true;
}

bool resetCardState() {
    auto view = ecs::View<CardData, CardData::State>();
    for (auto it = view.begin(); it != view.end(); ++it) {
        auto [st]    = *it;
        st->phase    = CardState::Deck;
        st->hovered  = false;
        st->dragging = false;
    }
    return true;
}

}  // namespace eve::card
