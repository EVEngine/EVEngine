// Native adaptation of pixelfx fx/moves/earth-stomp.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Chain.h"
namespace eve::stylize::pixelfx {
const Descriptor& earthStompDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::EarthStomp, "moves/earth-stomp", 290, 142, 50, 128, .06 + 3.4, false},
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
            static const std::array<ChainCue, 1> cues{{{PixelFxKind::EarthSpike, .06, INFINITY, 112}}};
            return std::make_unique<ChainRecipe>(cues, s.seed);
        }};
    return d;
}
}  // namespace eve::stylize::pixelfx
