// eve-hot-path
// Adapted from pixelfx projectiles/rocket.js (MIT).
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/PixelFxInternal.h"
#include "stylize/pixelfx/Projectile.h"
namespace eve::stylize::pixelfx {
namespace {
// Authored solid geometry and cardinal outline are generated at compile time, never baked image frames.
constexpr auto makeShip() {
    std::array<std::array<char, 30>, 19> g{};
    for (auto& row : g) row.fill('.');
    auto set = [&](int x, int y, char c) {
        if (x >= -15 && x < 15 && y >= -9 && y < 10) g[size_t(y + 9)][size_t(x + 15)] = c;
    };
    constexpr std::array<char, 7> body{{'4', '6', '5', '4', '3', '2', '1'}}, seam{{'3', '4', '4', '3', '2', '1', '1'}},
        paint{{'d', 'e', 'e', 'd', 'c', 'b', 'a'}};
    for (int y = -3; y <= 3; ++y)
        for (int x = -11; x <= 5; ++x) set(x, y, body[size_t(y + 3)]);
    for (int y = -3; y <= 3; ++y) {
        set(-3, y, seam[size_t(y + 3)]);
        set(-9, y, paint[size_t(y + 3)]);
        set(-8, y, paint[size_t(y + 3)]);
    }
    constexpr std::array<int, 8> heights{{3, 3, 2, 2, 1, 1, 0, 0}};
    for (int x = 6; x <= 13; ++x)
        for (int y = -heights[size_t(x - 6)]; y <= heights[size_t(x - 6)]; ++y) set(x, y, paint[size_t(y + 3)]);
    set(7, -2, 'w');
    set(8, -2, 'w');
    set(9, -1, 'e');
    set(13, 0, 'e');
    for (int i = 0; i < 4; ++i) {
        const int end = i == 0 ? -8 : i == 1 ? -9 : i == 2 ? -10 : -11;
        for (int x = -13; x <= end; ++x) {
            set(x, -4 - i, x == end ? '5' : i > 1 ? '4' : '3');
            set(x, 4 + i, x == end ? '3' : i > 1 ? '1' : '2');
        }
    }
    for (int y = -2; y <= 2; ++y) set(-12, y, y < 0 ? '3' : '2');
    for (int y = -3; y <= 3; ++y) set(-13, y, y < -1 ? '2' : '1');
    set(-14, -3, 'h');
    set(-14, 3, 'h');
    for (int y = -2; y <= 2; ++y) set(-14, y, (y > -2 && y < 2) ? 'x' : 'h');
    auto solid = [&](int x, int y) {
        return x >= 0 && y >= 0 && x < 30 && y < 19 && g[size_t(y)][size_t(x)] != '.' && g[size_t(y)][size_t(x)] != 'o';
    };
    for (int y = 0; y < 19; ++y)
        for (int x = 0; x < 30; ++x)
            if (g[size_t(y)][size_t(x)] == '.' &&
                (solid(x - 1, y) || solid(x + 1, y) || solid(x, y - 1) || solid(x, y + 1)))
                g[size_t(y)][size_t(x)] = 'o';
    return g;
}
constexpr auto SHIP      = makeShip();
constexpr auto SHIP_ROWS = []() {
    std::array<std::string_view, 19> rows{};
    for (size_t i = 0; i < 19; ++i) rows[i] = std::string_view(SHIP[i].data(), 30);
    return rows;
}();
void jet(Paint& p, std::span<const uint32_t> F, double x0, double y0, double J, double wob, uint32_t fr) {
    std::array<double, 4> diamonds{};
    for (uint32_t i = 0; i < 4; ++i) diamonds[i] = 3.6 + i * 4.3 + (rand({fr, i, 41}) - .5) * .7;
    p.shade(x0 - J - 2, y0 - 6, x0, y0 + 6, [&](double x, double y, int sx, int sy) {
        const double u = x0 - x;
        if (u < 0) return;
        const double q = u / J;
        if (q > 1.05) return;
        const double ady = std::abs(y - y0), qq = std::min(q, 1.0),
                     w = (2.6 + 2.4 * std::pow(qq, .7)) * (1 - qq * qq) * wob;
        const double m =
            w - ady +
            (rand({jsUint(std::floor(x) * 7 + std::floor(y) * 131), fr}) - .5) * .9 * std::min(1.0, .25 + q * 2);
        if (m < -.5) return;
        if (m < .5) {
            p.put(sx, sy, F[q < .45 ? 3 : 2]);
            return;
        }
        for (size_t i = 0; i < diamonds.size(); ++i) {
            if (diamonds[i] > J * .72) break;
            const double d = std::abs(u - diamonds[i]) / (1.9 - double(i) * .15) + ady / (1.7 - double(i) * .25);
            if (d < 1) {
                p.put(sx, sy, d < .55 && i < 3 ? F[8] : F[7]);
                return;
            }
        }
        if (u < 1.6 && ady < 1.6) {
            p.put(sx, sy, F[8]);
            return;
        }
        const double level = (1 - ady / std::max(.6, w)) * (1 - std::pow(qq, 1.5));
        p.put(sx, sy, level > .62 ? (u < 3 ? F[7] : F[5]) : level > .38 ? F[5] : level > .16 ? F[4] : F[3]);
    });
}
void rocket(Paint& p, double t, const PixelFxSettings& o) {
    const auto&    F  = o.palette;
    const auto&    M  = o.projectile.metal;
    const auto&    A  = o.projectile.paint;
    const auto&    SM = o.projectile.smoke;
    const uint32_t S = o.seed, fr = jsUint(std::floor(t * 30 + 1e-6));
    const double   D = 1.2, V = o.projectile.speed, NOZZLE = -14, J0 = 22, left = -250;
    auto           bob = [&](double tt) { return jsRound(std::sin((tt / D) * TAU) * .62); };
    const double by = bob(t), J = J0 * (.84 + .32 * rand({fr, S, 1})), wob = .92 + .16 * rand({fr, S, 2}), fl = J / J0;
    const std::array<uint32_t, 4> SP{{F[7], F[6], F[4], F[3]}};
    std::vector<projectile::Puff> list;
    cycle(
        t, D, 66, S + 1, 1.1, 1.45,
        [&](double age, double life, Rng& r, int) {
            const double x0 = NOZZLE - J0 * r.range(.62, .8), y0 = bob(t - age) + r.range(-1, 1),
                         vx = -r.range(75, 105), vy = r.range(-20, 20);
            const auto   m = projectile::wake(x0, y0, vx, vy, age, V, 3, 0);
            const double k = age / life, rise = .5 * r.range(6, 16) * age * age,
                         sway = std::sin(age * 2.3 + r.range(0, TAU)) * age * 2;
            const double rad0 = r.range(2.2, 3);
            const bool   big  = r.isChance(.25);
            const double rad  = rad0 + ((big ? r.range(14, 19) : r.range(8, 13)) - 2.6) * (1 - std::exp(-2.2 * age));
            if (m.x - rad < left + 1) return;
            const double a = std::min(1.0, age * 16) * std::pow(1 - k, 1.25) * .68 * smooth(left, left + 40, m.x - rad),
                         b = clamp(.95 - k * .75 + r.range(-.08, .08)), warm = std::exp(-age * r.range(3.5, 6));
            list.push_back({m.x, m.y - rise + sway, rad, a, b, warm});
        },
        true);
    const std::array<uint32_t, 7> warm{
        {mix(F[0], SM[0], .5), mix(F[1], SM[1], .45), mix(F[2], SM[2], .35), F[3], F[4], F[6], F[7]}};
    projectile::cloud(p, list, SM, warm, 1.3, .86, 1.5);
    p.glow(NOZZLE - J0 - 6, by, 26, F[4], .24 * fl);
    cycle(
        t, D, 9, S + 4, .6, 1.1,
        [&](double age, double life, Rng& r, int) {
            const double k = 1 - age / life, vx = -r.range(40, 120), vy = r.range(-40, 30);
            const auto   m = projectile::wake(NOZZLE - J0 * .6, bob(t - age), vx, vy, age, V, 2.5, -8);
            if (m.x < left + 2 || (k < .3 && rand({fr, uint32_t(r.integer(0, 999))}) < .5)) return;
            const uint32_t c = k > .6 ? F[6] : k > .3 ? F[4] : F[2];
            p.px(m.x, m.y, c);
            p.glow(m.x, m.y, 3, c, .35 * k);
        },
        true);
    cycle(
        t, D, 30, S + 3, .08, .16,
        [&](double age, double life, Rng& r, int) {
            const double k = age / life, x0 = NOZZLE - J0 * r.range(.6, .9), y0 = bob(t - age) + r.range(-1.5, 1.5),
                         vx = -r.range(10, 50), vy = r.range(-12, 12);
            const auto   m   = projectile::wake(x0, y0, vx, vy, age, V, 4, -30);
            const int    idx = 7 - int(std::floor(std::pow(k, .7) * 4));
            const double rad = r.range(1.2, 2.2) * (1 - k * .6);
            p.circle(m.x, m.y, rad, F[size_t(idx - 2)]);
            p.circle(m.x + rad * .25, m.y - rad * .1, rad * .65, F[size_t(idx)]);
            p.glow(m.x, m.y, rad * 2.4 + 2, F[size_t(idx)], .2 * (1 - k));
        },
        true);
    cycle(
        t, D, 30, S + 2, .18, .45,
        [&](double age, double life, Rng& r, int) {
            const double x0 = NOZZLE - r.range(2, J0 * .7), y0 = bob(t - age) + r.range(-1.5, 1.5),
                         vx = -r.range(20, 190), vy = r.range(-80, 50);
            const auto   m = projectile::wake(x0, y0, vx, vy, age, V, 2.6, 160);
            if (m.x > left + 10) projectile::spark(p, m, 1 - age / life, SP);
        },
        true);
    cycle(
        t, D, 8, S + 6, .5, .8,
        [&](double age, double life, Rng& r, int) {
            const double x0 = NOZZLE - r.range(4, J0 * .8), y0 = bob(t - age) + r.range(-1, 1), vx = -r.range(10, 80),
                         vy = r.range(-60, 10);
            const auto   m  = projectile::wake(x0, y0, vx, vy, age, V, 1.8, 170);
            if (m.x > left + 10) projectile::spark(p, m, 1 - age / life, SP, .5);
        },
        true);
    jet(p, F, NOZZLE, by + .5, J, wob, fr + S * 31);
    std::array<uint32_t, 256> pal{};
    pal['o'] = hex(0x100d17);
    for (size_t i = 1; i <= 6; ++i) pal[size_t('0') + i] = M[i];
    for (size_t i = 0; i < 5; ++i) pal[size_t('a') + i] = A[i];
    pal['w'] = WHITE;
    pal['h'] = F[5];
    pal['x'] = F[8];
    p.sprite(SHIP_ROWS, pal, -15, -9 + by);
    p.glow(NOZZLE - 10, by, 60, F[4], .28 * fl);
    p.glowLine(NOZZLE - 3, by + .5, NOZZLE - J * .9, by + .5, 11, F[4], .2);
    p.glow(NOZZLE - 1, by + .5, 8, F[7], .45);
    p.glow(NOZZLE + 3, by + 5, 12, F[4], .22 * fl);
}
}  // namespace
const Descriptor& rocketDescriptor() {
    static const std::array<Option, 4> options{{{"speed", 1, 1000, false, 0, &PixelFxProjectileSettings::speed},
                                                {"metal", 1, 64, false, 7, &PixelFxProjectileSettings::metal},
                                                {"paint", 1, 64, false, 5, &PixelFxProjectileSettings::paint},
                                                {"smoke", 1, 64, false, 7, &PixelFxProjectileSettings::smoke}}};
    static const Descriptor            descriptor{
        {PixelFxKind::Rocket, "projectiles/rocket", 288, 76, 252, 42, 1.2, true},
        9,
        [](PixelFxSettings& s) {
            s.seed             = 5;
            s.projectile.speed = 120;
            s.palette          = {hex(0x4a1712), hex(0x8a1f17), hex(0xc9281f), hex(0xf2561d), hex(0xff8a2a),
                                  hex(0xffc23d), hex(0xffe26b), hex(0xfff3a6), WHITE};
            s.projectile.metal = {hex(0x0f0d16), hex(0x25243a), hex(0x3e4058), hex(0x61667f),
                                  hex(0x8e97b0), hex(0xc4cce0), hex(0xf2f6ff)};
            s.projectile.paint = {hex(0x3a0d14), hex(0x7c1a22), hex(0xbd2f2c), hex(0xec5a45), hex(0xff9b7e)};
            s.projectile.smoke = {hex(0x26222e), hex(0x3d3846), hex(0x5a5463), hex(0x7e7887),
                                  hex(0xa7a1ad), hex(0xd0cad2), hex(0xf1ecee)};
        },
        &rocket,
        options};
    return descriptor;
}
}  // namespace eve::stylize::pixelfx
