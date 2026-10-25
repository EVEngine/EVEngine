#pragma once

#include "graphics/DamageNumbers.h"

#include <array>

namespace eve::graphics {
struct DamageNumbers::Impl {
    struct Popup {
        DamageNumberRequest request;
        DamageNumberStyle   style;
        std::string         text;
        double              age  = 0.0;
        float               lane = 0.f;
    };
    std::array<DamageNumberStyle, 4> styles;
    std::vector<Popup>               items;
    std::size_t                      sequence = 0;
    static DamageNumberSample        sample(const Popup& popup);
};
}  // namespace eve::graphics
