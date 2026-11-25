// eve-hot-path
// Native adaptation of pixelfx scifi/glitch.js, MIT.
// Copyright (c) 2026 Patch; see LICENSE.txt.
#include "stylize/pixelfx/Runtime.h"
namespace eve::stylize::pixelfx {
namespace {
struct Burst {
    int    first, last;
    double strength;
};
constexpr std::array<Burst, 3> bursts{{{9, 16, .8}, {33, 37, .5}, {51, 62, 1}}};
struct Tear {
    double y, height, dx, tint;
};
struct Glitch final : AnalyticKernel {
    PixelFxActorVisibility actorVisibility(double) const override { return PixelFxActorVisibility::Hidden; }
    void                   drawBack(Paint&, double, const PixelFxSettings&) const override {}
    void                   draw(Paint& p, double t, const PixelFxSettings& o) const override {
        const auto&    C = o.palette;
        const auto&    A = o.actor;
        const uint32_t S = o.seed, RED = C[2], CYAN = C[4], f = uint32_t(std::floor(t * 30 + 1e-6)) % 72;
        const Burst*   burst = nullptr;
        for (const auto& b : bursts)
            if (f >= uint32_t(b.first) && f <= uint32_t(b.last)) {
                burst = &b;
                break;
            }
        const double k     = burst ? burst->strength : 0;
        const auto   r     = [&]<class... N>(N... n) { return rand({S, f, uint32_t(n)...}); };
        const double split = burst ? jsRound(1 + r(1) * 3 * k) * (r(2) < .5 ? -1 : 1) : 0;
        // First matching tear wins, unlike the last-write-wins row map in EMP blast.
        std::array<Tear, 5> tears{};
        size_t              count = 0;
        if (burst)
            for (int i = 0; i < 1 + std::floor(r(3) * 4 * k); ++i)
                tears[count++] = {A.y + std::floor(r(4, i) * A.height), 1 + std::floor(r(5, i) * 5),
                                  jsRound((r(6, i) - .5) * 16 * k), r(7, i)};
        if (!burst && r(8) < .07) tears[count++] = {A.y + std::floor(r(9) * A.height), 1, r(10) < .5 ? -2. : 2., 1};
        const bool interlace = burst && r(11) < .3, poster = burst && r(14) < .15 * k;
        const int  roll   = burst && r(12) < .15 ? -2 - int(std::floor(r(13) * 2)) : 0;
        const auto tearAt = [&](double y) -> const Tear* {
            for (size_t i = 0; i < count; ++i)
                if (y >= tears[i].y && y < tears[i].y + tears[i].height) return &tears[i];
            return nullptr;
        };
        const double bar = A.y + std::fmod((t / 2.4) * 2 * (A.height + 12), A.height + 12) - 6;
        if (split != 0)
            for (int my = 0; my < A.height; ++my)
                for (int mx = 0; mx < A.width; ++mx) {
                    if (!A.mask[size_t(my) * size_t(A.width) + size_t(mx)]) continue;
                    const double y  = A.y + my + roll;
                    const auto*  tr = tearAt(A.y + my);
                    const double dx = tr ? tr->dx : 0;
                    p.px(A.x + mx + dx - split, y, RED);
                    p.px(A.x + mx + dx + split, y, CYAN);
                }
        for (int my = 0; my < A.height; ++my) {
            const double ry = A.y + my, y = ry + roll;
            if (interlace && (my & 1)) continue;
            const auto*    tr = tearAt(ry);
            const double   dx = tr ? tr->dx : 0, near = std::abs(ry - bar);
            const uint32_t tint = tr && tr->tint < .35 ? (tr->tint < .18 ? RED : CYAN) : 0;
            for (int mx = 0; mx < A.width; ++mx) {
                const uint32_t c = A.pixels[size_t(my) * size_t(A.width) + size_t(mx)];
                if (!c) continue;
                const uint32_t col = poster     ? (luma(c) > .32 ? C[5] : C[1])
                                     : tint     ? mix(c, tint, .6)
                                     : near < 1 ? mix(c, C[5], .35)
                                                : c;
                p.px(A.x + mx + dx, y, col);
            }
            if (tr && std::abs(dx) > 2 && rand({S, f, jsUint(ry)}) < .6) {
                const bool right = dx > 0;
                int        mx    = right ? A.width : -1;
                for (int i = 0; i < A.width; ++i) {
                    const int x = right ? A.width - 1 - i : i;
                    if (A.mask[size_t(my) * size_t(A.width) + size_t(x)]) {
                        mx = x;
                        break;
                    }
                }
                if (mx >= 0 && mx < A.width) {
                    const uint32_t c = A.pixels[size_t(my) * size_t(A.width) + size_t(mx)];
                    const int      L = 4 + int(std::floor(rand({S, f, jsUint(ry), 1}) * 10));
                    for (int i = 1; i <= L; ++i) p.dot(A.x + mx + dx + (right ? i : -i), y, c, 1 - double(i) / (L + 1));
                }
            }
        }
        // Source out-of-range pixels are undefined/falsy when a one-row actor rolls by two or three rows.
        if (roll)
            for (int my = 0; my < -roll && my < A.height; ++my)
                for (int mx = 0; mx < A.width; ++mx) {
                    const uint32_t c = A.pixels[size_t(my) * size_t(A.width) + size_t(mx)];
                    if (c && ((mx + my) & 1)) p.px(A.x + mx, A.y + A.height + roll + my, mix(c, C[1], .5));
                }
        if (burst)
            for (int i = 0; i < 2 + std::floor(r(20) * 5 * k); ++i) {
                const int    w = 2 + int(std::floor(r(21, i) * 9)), h = 1 + int(std::floor(r(22, i) * 3));
                const double x = A.x - 6 + std::floor(r(23, i) * (A.width + 12)),
                             y = A.y + std::floor(r(24, i) * A.height);
                const std::array<uint32_t, 5> colors{WHITE, RED, CYAN, C[5], C[3]};
                const uint32_t                c = colors[size_t(std::floor(r(25, i) * 5))];
                if (r(26, i) < .4)
                    for (int yy = 0; yy < h; ++yy)
                        for (int xx = 0; xx < w; ++xx) p.px(x + xx, y + yy, (xx + yy) & 1 ? c : C[0]);
                else
                    p.rect(x, y, w, h, c);
            }
        for (const auto& b : bursts) {
            const int age = (int(f) - b.first + 72) % 72;
            if (age > b.last - b.first + 8) continue;
            for (int i = 0; i < 10 * b.strength; ++i) {
                const int life = 4 + int(std::floor(rand({S, uint32_t(b.first), uint32_t(i)}) * 6)),
                          at   = int(std::floor(rand({S, uint32_t(b.first), uint32_t(i), 1}) * (b.last - b.first + 1))),
                          q    = age - at;
                if (q < 0 || q > life || (q > double(life) / 2 && (q & 1))) continue;
                const double                  x0   = A.x + rand({S, uint32_t(b.first), uint32_t(i), 2}) * A.width,
                                              y0   = A.y + rand({S, uint32_t(b.first), uint32_t(i), 3}) * A.height,
                                              side = rand({S, uint32_t(b.first), uint32_t(i), 4}) < .5 ? -1 : 1;
                const std::array<uint32_t, 3> colors{WHITE, RED, CYAN};
                p.px(x0 + side * q * (1 + rand({S, uint32_t(b.first), uint32_t(i), 5}) * 2), y0 - q * .5,
                     colors[size_t(i % 3)]);
            }
        }
        if (burst) {
            if (f == uint32_t(burst->first))
                p.glow(A.x + double(A.width) / 2, A.y + double(A.height) / 2, 26, C[5], .4);
            p.glow(A.x + double(A.width) / 2 - split * 4, A.y + double(A.height) / 2, 18, RED, .18 * k);
            p.glow(A.x + double(A.width) / 2 + split * 4, A.y + double(A.height) / 2, 18, CYAN, .18 * k);
        }
        if (std::abs(bar - (A.y + double(A.height) / 2)) < double(A.height) / 2 + 3)
            p.glowLine(A.x - 2, bar, A.x + A.width + 2, bar, 4, C[4], .12);
    }
};
}  // namespace
const Descriptor& glitchDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::Glitch, "scifi/glitch", 88, 64, 44, 54, 2.4, true},
        7,
        [](PixelFxSettings& s) {
            s.seed         = 7;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x14101f), hex(0x3b1f5c), hex(0xff2e63), hex(0xff7aa8),
                              hex(0x2ef2ff), hex(0xb4fbff), WHITE};
        },
        nullptr,
        options,
        nullptr,
        nullptr,
        [](const PixelFxSettings&) -> std::unique_ptr<const AnalyticKernel> { return std::make_unique<Glitch>(); },
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
