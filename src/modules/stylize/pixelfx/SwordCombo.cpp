// Native adaptation of pixelfx fx/moves/sword-combo.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Chain.h"
namespace eve::stylize::pixelfx {
const Descriptor& swordComboDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::SwordCombo, "moves/sword-combo", 264, 150, 72, 130, (1.04 + .335) + 1.6, false},
        8,
        [](PixelFxSettings& s) {
            s.seed = 1;
            s.palette.assign(8, hex(0x8fb4ff));
            s.actor.source = PixelFxActorSource::Dummy;
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        nullptr,
        false,
        0,
        1,
        {},
        [](const PixelFxSettings& s) -> std::unique_ptr<const SimulationRecipe> {
            static const std::array<ChainCue, 3> cues{{{PixelFxKind::SwordSlash},
                                                       {PixelFxKind::DoubleSlash, .5, INFINITY, 10},
                                                       {PixelFxKind::HeavyCleave, 1.04, INFINITY, 20}}};
            return std::make_unique<ChainRecipe>(cues, s.seed);
        }};
    return d;
}
}  // namespace eve::stylize::pixelfx
