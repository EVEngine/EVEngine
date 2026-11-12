// eve-hot-path
// Native adaptation of pixelfx scifi/scanner.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt.
#include <map>
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScifiGhosts.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 3.2, LOCK = .3, SCAN0 = .42, SCAN1 = 1.6, DONE = 1.75, LET = 2.5;
double           scanT(double y, const PixelFxActor& a) {
    return SCAN0 + ((SCAN1 - SCAN0) * std::acos(1 - 2 * clamp((y + .5 - (a.y - 3)) / (4 - a.y)))) / PI;
}
struct Scanner final : AnalyticKernel {
    const PixelFxActor                      actor;
    std::vector<Point>                      edge;
    std::map<double, std::array<double, 2>> rows;
    scifi::Ghosts                           ghosts{D, 0, std::array<double, 4>{-40, -58, 68, 9}};
    explicit Scanner(const PixelFxSettings& o) : actor(o.actor) {
        const auto on = [&](int x, int y) {
            return x >= 0 && y >= 0 && x < actor.width && y < actor.height &&
                   actor.mask[size_t(y) * size_t(actor.width) + size_t(x)] > 0;
        };
        for (int y = -1; y <= actor.height; ++y)
            for (int x = -1; x <= actor.width; ++x) {
                if (on(x, y)) {
                    const double X = actor.x + x, Y = actor.y + y;
                    auto [it, inserted] = rows.try_emplace(Y, std::array<double, 2>{X, X});
                    if (!inserted) {
                        it->second[0] = std::min(it->second[0], X);
                        it->second[1] = std::max(it->second[1], X);
                    }
                } else if (on(x - 1, y) || on(x + 1, y) || on(x, y - 1) || on(x, y + 1))
                    edge.push_back({actor.x + x, actor.y + y});
            }
        for (double y = actor.y; y < 0; y += 3) {
            const auto row = rows.find(y);
            if (row == rows.end()) continue;
            for (int side = 0; side < 2; ++side) {
                const double   s = side == 0 ? -1 : 1, x = row->second[size_t(side)] + s;
                const uint32_t Y = jsUint(y), S = jsUint(s);
                if (rand({o.seed, Y, S}) < .6)
                    ghosts.add(scanT(y, actor), ParticleKind::Mote, x, y, s * (30 + 50 * rand({Y, S, 3})),
                               -10 + 20 * rand({Y, S, 4}), .2 + .15 * rand({Y, S, 5}),
                               rand({Y, S, 6}) < .4 ? WHITE : o.palette[5], .6);
            }
        }
    }
    static void bracket(Paint& p, double x, double y, double sx, double sy, double L, uint32_t c, uint32_t hot,
                        double a) {
        p.line(x, y, x + sx * L, y, c, a);
        p.line(x, y, x, y + sy * L, c, a);
        p.line(x + sx, y + sy, x + sx * 3, y + sy, c, a * .6);
        p.dot(x, y, hot, a);
    }
    void drawBack(Paint&, double, const PixelFxSettings&) const override {}
    void draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&    C   = o.palette;
        const auto&    A   = actor;
        const double   bx0 = A.x - 4, bx1 = A.x + A.width + 3, by0 = A.y - 4, by1 = 3,
                       lock = ease::outBack(clamp(t / LOCK), 2.2), out = ease::inCubic(clamp((t - LET) / .45)),
                       spread = (1 - lock) * 14 + out * 12;
        const bool     gone   = t > LET + .45 || (t > LET + .25 && int(std::floor((t - LET) * 20)) % 2 == 1),
                       flash  = (t > LOCK && t < LOCK + .07) || (t > DONE && t < DONE + .05) ||
                                (t > DONE + .12 && t < DONE + .17);
        const double   vis    = gone ? 0 : 1;
        const uint32_t bc     = flash ? WHITE : C[5];
        if (vis > 0)
            for (const auto& b : std::array<std::array<double, 4>, 4>{{{bx0 - spread, by0 - spread, 1, 1},
                                                                       {bx1 + spread, by0 - spread, -1, 1},
                                                                       {bx0 - spread, by1 + spread, 1, -1},
                                                                       {bx1 + spread, by1 + spread, -1, -1}}}) {
                bracket(p, b[0], b[1], b[2], b[3], 7, bc, WHITE, vis);
                p.glow(b[0], b[1], 6, C[4], .35 * vis);
            }
        const double lk = (t - LOCK) / .3;
        if (lk >= 0 && lk < 1) {
            const double   e = 3 + 10 * ease::outCubic(lk);
            const uint32_t c = lk < .25 ? WHITE : lk < .5 ? C[6] : lk < .75 ? C[5] : C[3];
            p.line(bx0 - e, by0 - e, bx1 + e, by0 - e, c);
            p.line(bx0 - e, by1 + e, bx1 + e, by1 + e, c);
            p.line(bx0 - e, by0 - e, bx0 - e, by1 + e, c);
            p.line(bx1 + e, by0 - e, bx1 + e, by1 + e, c);
        }
        const double age = scifi::since(t, DONE, D), pulse = age < .3 ? 1 - age / .3 : 0,
                     offY = lerp(A.y - 2, 2, clamp((t - LET) / .4));
        if (t > SCAN0)
            for (const auto& point : edge) {
                const auto [x, y] = point;
                const double a    = t - scanT(y, A);
                if (a < 0 || (t > LET && y < offY)) continue;
                p.dot(x, y,
                      pulse > .4     ? WHITE
                      : a < .07      ? WHITE
                      : a < .25      ? C[6]
                      : a < .6       ? C[5]
                      : t > LET - .3 ? C[3]
                                     : C[4]);
            }
        if (t > SCAN0 && t < SCAN1) {
            const double y = std::floor(lerp(A.y - 3, 1, ease::inOutSine(clamp((t - SCAN0) / (SCAN1 - SCAN0)))));
            for (int k = 1; k <= 5; ++k) {
                const auto r = rows.find(y - k);
                if (r == rows.end()) continue;
                for (double x = r->second[0]; x <= r->second[1]; ++x) {
                    const double index = (y - k - A.y) * A.width + x - A.x;
                    if (index >= 0 && index < double(A.mask.size()) && index == std::floor(index) &&
                        A.mask[size_t(index)])
                        p.blend(x + .5, y - k + .5, k < 2 ? C[6] : C[5], .55 - k * .09);
                }
            }
            const auto r = rows.find(y);
            for (double x = bx0 - 2; x <= bx1 + 2; ++x) {
                const bool inside = r != rows.end() && x >= r->second[0] && x <= r->second[1];
                p.dot(x, y, inside ? WHITE : C[6]);
                p.blend(x + .5, y - .5, C[5], .35);
            }
            p.glowLine(bx0, y, bx1, y, 6, C[5], .45);
            if (r != rows.end())
                for (double x : {r->second[0] - .5, r->second[1] + 1.5}) p.sparkle(x, y + .5, 2, C[6], C[5], .3);
        }
        const double lead = clamp((t - LOCK) / .1);
        if (vis > 0 && lead > 0) {
            const double px = bx1 + spread + 6, py = by0 - spread + 2, k = clamp((t - SCAN0) / (SCAN1 - SCAN0));
            const bool   ok = t > DONE;
            p.line(bx1 + spread, by0 - spread, lerp(bx1 + spread, px - 1, lead), lerp(by0 - spread, py + 4, lead),
                   C[3]);
            if (lead < 1) {
                ghosts.evaluate(t).draw(p);
                return;
            }
            p.line(px - 1, py + 4, px + 1, py + 4, C[3]);
            const double      del     = clamp((t - LET) / .25);
            const std::string full    = ok ? "OK" : std::to_string(int(std::floor(k * 100))) + "%";
            const auto        label   = full.substr(0, size_t(std::ceil(double(full.size()) * (1 - del))));
            const bool        blinkOk = ok && int(std::floor((t - DONE) * 8)) % 2 == 0 && t < DONE + .5;
            if (!label.empty()) {
                TextOptions opts;
                opts.scale  = 2;
                opts.shadow = C[1];
                p.text(label, px + 2, py, blinkOk ? WHITE : C[6], opts);
            }
            for (int i = 0; i < 10; ++i) {
                if (i >= 10 * (1 - del)) break;
                const bool on = (i + 1) / 10. <= k + 1e-6 || ok;
                p.rect(px + 2 + i * 3, py + 13, 2, 3, on ? (ok ? C[6] : C[5]) : C[2]);
            }
            p.glow(px + 16, py + 6, 18, C[4], .2 * vis);
        }
        ghosts.evaluate(t).draw(p);
    }
};
}  // namespace
const Descriptor& scannerDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::Scanner, "scifi/scanner", 112, 78, 42, 58, D, true},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 22;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x03140a), hex(0x06301a), hex(0x0c5a2c), hex(0x16893e),
                              hex(0x2cc256), hex(0x7cf08c), hex(0xd0ffd4), WHITE};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings& s) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<Scanner>(s); },
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
