// Native adaptation of pixelfx fx/moves/pistol-shots.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Chain.h"
namespace eve::stylize::pixelfx {
const Descriptor& pistolShotsDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{{PixelFxKind::PistolShots, "moves/pistol-shots", 378, 76, 111, 72, 2.6, false},
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
                                  static const std::array<ChainCue, 9> cues{
                                      {{PixelFxKind::MuzzlePistol, .14, INFINITY, 28, 0, nullptr,
                                        [](PixelFxSettings& s) { s.projectile.height = 32; }},
                                       {PixelFxKind::BulletTracer, .14, .14 + .12, 0, -32,
                                        [](double t) { return 28 + 4 + (t - .14) * ((180 - 28 - 14) / .12); }},
                                       {PixelFxKind::HitSpark, .14 + .12, INFINITY, 170, 0, nullptr,
                                        [](PixelFxSettings& s) { s.impact.height = 28; }},
                                       {PixelFxKind::MuzzlePistol, .42, INFINITY, 28, 0, nullptr,
                                        [](PixelFxSettings& s) { s.projectile.height = 32; }},
                                       {PixelFxKind::BulletTracer, .42, .42 + .12, 0, -32,
                                        [](double t) { return 28 + 4 + (t - .42) * ((180 - 28 - 14) / .12); }},
                                       {PixelFxKind::HitSpark, .42 + .12, INFINITY, 170, 0, nullptr,
                                        [](PixelFxSettings& s) { s.impact.height = 28; }},
                                       {PixelFxKind::MuzzlePistol, .7, INFINITY, 28, 0, nullptr,
                                        [](PixelFxSettings& s) { s.projectile.height = 32; }},
                                       {PixelFxKind::BulletTracer, .7, .7 + .12, 0, -32,
                                        [](double t) { return 28 + 4 + (t - .7) * ((180 - 28 - 14) / .12); }},
                                       {PixelFxKind::HitSpark, .7 + .12, INFINITY, 170, 0, nullptr,
                                        [](PixelFxSettings& s) { s.impact.height = 28; }}}};
                                  return std::make_unique<ChainRecipe>(cues, s.seed);
                                         }};
    return d;
}
}  // namespace eve::stylize::pixelfx
