// Native adaptation of pixelfx fx/moves/ice-lance-throw.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Chain.h"
namespace eve::stylize::pixelfx {
const Descriptor& iceLanceThrowDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::IceLanceThrow, "moves/ice-lance-throw", 310, 136, 86, 134, (.8 + .34) + 3.2, false},
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
            static const std::array<ChainCue, 3> cues{{{PixelFxKind::SpellCharge, .06, .8, 0, 0, nullptr,
                                                        [](PixelFxSettings& s) {
                                                            s.palette = {hex(0x0c1430), hex(0x162c5c), hex(0x1f4f94),
                                                                         hex(0x2f7fcc), hex(0x58b4ec), hex(0x9bdcff),
                                                                         hex(0xd8f4ff), WHITE};
                                                        }},
                                                       {PixelFxKind::IceLance, .8, .8 + .34, 0, -24,
                                                        [](double t) { return 34 + (t - .8) * ((170 - 34) / .34); }},
                                                       {PixelFxKind::SnowflakeBurst, .8 + .34, INFINITY, 170, -26}}};
            return std::make_unique<ChainRecipe>(cues, s.seed);
        }};
    return d;
}
}  // namespace eve::stylize::pixelfx
