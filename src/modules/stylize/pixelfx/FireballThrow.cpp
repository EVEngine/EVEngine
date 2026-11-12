// Native adaptation of pixelfx fx/moves/fireball-throw.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Chain.h"
namespace eve::stylize::pixelfx {
const Descriptor& fireballThrowDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::FireballThrow, "moves/fireball-throw", 356, 244, 64, 198, (.95 + .46) + 5.4, false},
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
            static const std::array<ChainCue, 3> cues{
                {{PixelFxKind::SpellCharge, .08, .95, 0, 0, nullptr,
                  [](PixelFxSettings& s) {
                      s.palette = {hex(0x1f0b10), hex(0x4a1118), hex(0x8c1d1c), hex(0xcc3a1a),
                                   hex(0xf2701f), hex(0xffae35), hex(0xffe27a), hex(0xfff8d6)};
                  }},
                 {PixelFxKind::Fireball, .95, .95 + .46, 0, -22, [](double t) { return 28 + (t - .95) * 340; }},
                 {PixelFxKind::SmallExplosion, .95 + .46, INFINITY, 190}}};
            return std::make_unique<ChainRecipe>(cues, s.seed);
        }};
    return d;
}
}  // namespace eve::stylize::pixelfx
