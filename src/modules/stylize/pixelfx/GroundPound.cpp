// Native adaptation of pixelfx fx/moves/ground-pound.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Chain.h"
namespace eve::stylize::pixelfx {
const Descriptor& groundPoundDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor d{{PixelFxKind::GroundPound, "moves/ground-pound", 304, 204, 152, 168, .62 + 3.2, false},
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
                                  static const std::array<ChainCue, 1> cues{{{PixelFxKind::GroundSlam, .62}}};
                                  return std::make_unique<ChainRecipe>(cues, s.seed);
                              }};
    return d;
}
}  // namespace eve::stylize::pixelfx
