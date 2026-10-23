#pragma once
#include <algorithm>
#include "card/CardTypes.h"
namespace eve::card::card_internal {
inline bool ownsCard(const std::vector<ecs::EntityHandle> &handles, const CardData &card) {
    const auto live = ecs::handle_of(const_cast<CardData *>(&card));
    return std::any_of(handles.begin(), handles.end(), [&live](const auto &handle) {
        return handle.table == live.table && handle.type == live.type && handle.id == live.id &&
               handle.generation == live.generation;
    });
}

}  // namespace eve::card::card_internal

namespace eve::card::card_internal {
void exposeStageBindings(ssq::Class &cls);
}
