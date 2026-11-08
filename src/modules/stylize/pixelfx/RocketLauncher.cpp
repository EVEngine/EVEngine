// Native adaptation of pixelfx fx/moves/rocket-launcher.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Chain.h"
namespace eve::stylize::pixelfx {
const Descriptor& rocketLauncherDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::RocketLauncher, "moves/rocket-launcher", 480, 244, 178, 198, (.6 + .56) + 5.4, false},
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
                {{PixelFxKind::MuzzleShotgun, .6, INFINITY, 34, 0, nullptr,
                  [](PixelFxSettings& s) { s.projectile.height = 21; }},
                 {PixelFxKind::Rocket, .6, .6 + .56, 0, -21,
                  [](double t) { return 34 + 10 + std::pow(t - .6, 1.5) * ((200 - 34 - 16) / std::pow(.56, 1.5)); }},
                 {PixelFxKind::SmallExplosion, .6 + .56, INFINITY, 200}}};
            return std::make_unique<ChainRecipe>(cues, s.seed);
        }};
    return d;
}
}  // namespace eve::stylize::pixelfx
