// eve-hot-path
// Native adaptation of pixelfx transitions/glitch-wipe.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScreenTransition.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double TICK = 1. / 18, IN0 = .03, IN1 = .78, OUT0 = .98, OUT1 = 1.66;
struct Band {
    uint32_t i;
    int      y, h, dir;
    double   off, t0, t1, o0, o1;
};
struct GlitchWipe final : AnalyticKernel {
    const std::vector<Band>  bands;
    static std::vector<Band> prepare(uint32_t seed) {
        std::vector<Band> out;
        out.reserve(36);
        for (int y = 0, i = 0; y < screen::H; ++i) {
            const auto   ix   = uint32_t(i);
            const double r    = rand({ix, seed});
            const int    h    = std::min(screen::H - y, r < .4    ? 5 + int(std::floor(rand({ix, seed + 1}) * 6))
                                                        : r < .85 ? 10 + int(std::floor(rand({ix, seed + 1}) * 10))
                                                                  : 20 + int(std::floor(rand({ix, seed + 1}) * 12)));
            const double a    = clamp(.5 + .55 * noise(y / 40., seed + 9) + (rand({ix, seed + 2}) - .5) * .25),
                         b    = clamp(.5 + .55 * noise(y / 40., seed + 19) + (rand({ix, seed + 3}) - .5) * .25);
            const double span = .32 + rand({ix, seed + 6}) * .12, t0 = IN0 + a * (IN1 - IN0 - span),
                         o0 = OUT0 + b * (OUT1 - OUT0 - span);
            out.push_back(
                {ix, y, h, rand({ix, seed + 4}) < .82 ? 1 : -1, rand({ix, seed + 5}), t0, t0 + span, o0, o0 + span});
            y += h;
        }
        return out;
    }
    explicit GlitchWipe(const PixelFxSettings& o) : bands(prepare(o.seed)) {}
    static double tear(const Band& b, double t, double t0, double t1, uint32_t salt) {
        if (t <= t0) return 0;
        if (t >= t1) return 1;
        uint32_t n = uint32_t(std::floor((t + b.off * TICK) / TICK));
        while (n > 0 && rand({n, b.i, salt}) < .22) --n;
        const double tq = n * TICK - b.off * TICK, u = clamp((tq - t0) / (t1 - t0));
        return clamp(ease::inOutQuad(u) + (rand({n, b.i, salt + 1}) - .6) * .12 * std::sin(PI * u));
    }
    void drawBack(Paint&, double, const PixelFxSettings&) const override {}
    void draw(Paint& p0, double t, const PixelFxSettings& o) const override {
        const auto&                   C   = o.palette;
        const auto                    ink = o.transition.color, RED = o.transition.split;
        auto                          p   = screen::clip(p0);
        const auto                    fr  = jsUint(std::floor(t / TICK));
        const bool                    out = t >= OUT0;
        const std::array<uint32_t, 4> colors{WHITE, C[6], C[5], RED};
        for (const auto& b : bands) {
            const double k = out ? tear(b, t, b.o0, b.o1, 7) : tear(b, t, b.t0, b.t1, 3);
            if ((!out && k <= 0) || (out && k >= 1)) continue;
            const bool   moving = k > 0 && k < 1;
            const double slip   = moving && rand({fr, b.i, 11}) < .12 ? jsRound((rand({fr, b.i, 12}) - .5) * 18) : 0;
            const int    fwd    = b.dir;
            const double e      = (b.dir > 0 ? k * screen::W : screen::W - k * screen::W) + slip;
            for (int y = b.y; y < b.y + b.h; ++y) {
                const double jag =
                    moving ? jsRound((rand({fr, uint32_t(y), 14}) - .5) * 6 + (rand({fr >> 1, b.i, 15}) - .5) * 6) : 0;
                const double ex     = clamp(jsRound(e + jag), 0, screen::W);
                const bool   behind = (fwd > 0) != out;
                if (behind) {
                    if (ex > 0) screen::rect(p0, 0, y, ex, 1, ink);
                } else if (ex < screen::W)
                    screen::rect(p0, ex, y, screen::W - ex, 1, ink);
                if (!moving) continue;
                const double e1   = behind ? ex - 1 : ex;
                const int    side = behind ? 1 : -1, sp = 2 + int((b.i + fr) % 3);
                p.px(e1, y, WHITE);
                p.px(e1 + side * sp, y, RED);
                p.px(e1 + side * (sp + 1), y, RED);
                p.px(e1 - side * (sp + 1), y, C[5]);
                p.px(e1 - side * (sp + 2), y, C[4]);
                p.light(e1, y, WHITE, .45);
                p.light(e1 + side * (sp + 1), y, RED, .5);
                p.light(e1 + side * (sp + 3), y, RED, .22);
                p.light(e1 - side * (sp + 1), y, C[5], .45);
                p.light(e1 - side * (sp + 3), y, C[5], .22);
            }
            if (!moving) continue;
            const double ex   = clamp(jsRound(e), 0, screen::W);
            const int    open = ((fwd > 0) != out) ? 1 : -1;
            p.glow(ex, b.y + b.h / 2., b.h * .7 + 7, (b.i & 1) ? RED : C[5], .28);
            for (uint32_t s = 0; s < uint32_t(b.h); ++s) {
                if (rand({fr, b.i, 100 + s}) > .55) continue;
                const double x = ex + open * jsRound(std::pow(rand({fr, b.i, 200 + s}), 2) * 22 - 4),
                             y = b.y + std::floor(rand({fr, b.i, 300 + s}) * b.h);
                p.px(x, y, colors[size_t(std::floor(rand({fr, b.i, 400 + s}) * 4))]);
            }
            for (uint32_t s = 0; s < 4; ++s) {
                if (rand({fr, b.i, 20 + s}) > .5) continue;
                const double len   = 2 + std::floor(std::pow(rand({fr, b.i, 30 + s}), 2) * 60);
                const bool   ahead = rand({fr, b.i, 40 + s}) < .6;
                const double d     = std::floor(std::pow(rand({fr, b.i, 50 + s}), 1.5) * 70) + 4,
                             y     = b.y + std::floor(rand({fr, b.i, 60 + s}) * b.h);
                const double x     = ahead ? (open > 0 ? ex + d : ex - d - len) : open > 0 ? ex - d - len : ex + d;
                const auto   c     = colors[size_t(std::floor(rand({fr, b.i, 70 + s}) * 4))];
                p.rect(x, y, len, 1, c);
                for (int q = 0; q < len; q += 3) p.light(x + q, y, c, .35);
            }
            if (b.h >= 8 && rand({fr, b.i, 80}) < .35) {
                const double bw = 6 + std::floor(rand({fr, b.i, 81}) * 10),
                             bh = 3 + std::floor(rand({fr, b.i, 82}) * std::min(6, b.h - 3));
                const double x  = ex + open * std::floor(rand({fr, b.i, 83}) * 24 - 4) - (open < 0 ? bw : 0),
                             y  = b.y + std::floor(rand({fr, b.i, 84}) * (b.h - bh));
                const auto   c  = rand({fr, b.i, 85}) < .5 ? C[5] : RED;
                p.rect(x, y, bw, 1, c);
                p.rect(x, y + bh - 1, bw, 1, c);
                p.rect(x, y, 1, bh, c);
                p.rect(x + bw - 1, y, 1, bh, c);
                for (int q = 0; q < bw; q += 2) p.light(x + q, y, c, .25);
            }
        }
        if (t > IN1 && t < OUT0 + .05)
            for (uint32_t s = 0; s < 3; ++s) {
                if (rand({fr, s, 90}) > .4) continue;
                const double y   = std::floor(rand({fr, s, 91}) * screen::H),
                             x   = std::floor(rand({fr, s, 92}) * screen::W),
                             len = 3 + std::floor(std::pow(rand({fr, s, 93}), 2) * 40);
                p.rect(x, y, len, 1, s == 0 ? C[4] : s == 1 ? C[3] : RED);
            }
    }
};
}  // namespace
const Descriptor& glitchWipeDescriptor() {
    static const std::array<Option, 2> options{
        {{"color", 0, 4294967295., false, 0, &PixelFxTransitionSettings::color, true},
         {"split", 0, 4294967295., false, 0, &PixelFxTransitionSettings::split, true}}};
    static const Descriptor d{{PixelFxKind::GlitchWipe, "transitions/glitch-wipe", 320, 180, 0, 0, 1.8, false},
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed             = 1;
                                  s.transition.color = hex(0x0b0a10);
                                  s.transition.split = hex(0xff2d6f);
                                  s.palette          = {hex(0x041418), hex(0x08303a), hex(0x0c5260), hex(0x108088),
                                                        hex(0x18b4b0), hex(0x4ce8d8), hex(0xb4fff0), WHITE};
                              },
                              nullptr,
                              options,
                              nullptr,
                              nullptr,
                              [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> {
                                  return std::make_unique<GlitchWipe>(s);
                              }};
    return d;
}
}  // namespace eve::stylize::pixelfx
