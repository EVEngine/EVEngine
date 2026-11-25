// Native adaptation of pixelfx fx/moves/arrow-shot.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Chain.h"
namespace eve::stylize::pixelfx {
const Descriptor& arrowShotDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::ArrowShot, "moves/arrow-shot", 300, 74, 38, 66, 1.9, false},
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
            static const std::array<ChainCue, 2> cues{{{PixelFxKind::Arrow, .56, .56 + .24, 0, -28,
                                                        [](double t) { return 30 + (t - .56) * ((180 - 30) / .24); }},
                                                       {PixelFxKind::HitSpark, .56 + .24, INFINITY, 170, 0, nullptr,
                                                        [](PixelFxSettings& child) { child.impact.height = 30; }}}};
            return std::make_unique<ChainRecipe>(cues, s.seed);
        }};
    return d;
}
}  // namespace eve::stylize::pixelfx
