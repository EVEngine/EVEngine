// eve-hot-path
// Native adaptation of pixelfx spawn/coin-drop.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double                          CY = -16, D = 3;
constexpr std::array<std::string_view, 8> COIN{"..oooo..", ".ohhlmo.", "ohlmmmdo", "ohmdhmdo",
                                               "olmdhmdo", "olmmmmdo", ".oddddo.", "..oooo.."};
constexpr std::array<std::string_view, 3> FLAT{".lhhhl.", "olmmmdo", ".odddo."};
struct Coin {
    double x, y, vx, vy, phase, spin;
    bool   rest = false;
    int    hits = 0;
    double out, glint, born;
    bool   gone   = false;
    double restAt = 0;
};
struct Ball {
    double x, y, vx, vy, radius, life, max;
};
struct Emission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color = WHITE;
    double       size = 1, delay = 0;
};
struct CoinDrop final : Kernel {
    const std::vector<uint32_t> C, SM;
    Rng                         R;
    Particles                   ps;
    Lights                      lights;
    std::array<uint32_t, 256>   PAL{};
    std::vector<Coin>           coins;
    std::vector<Ball>           balls;
    double                      t = 0;
    explicit CoinDrop(const PixelFxSettings& o) : C(o.palette), SM(o.spawn.smoke), R(o.seed) {
        PAL['o']  = C[1];
        PAL['h']  = C[6];
        PAL['l']  = C[5];
        PAL['m']  = C[4];
        PAL['d']  = C[3];
        ps.sparks = {WHITE, C[6], C[5], C[3]};
        for (int i = 0; i < 10; ++i) {
            const double a = (double(i) / 10) * TAU + R.range(-.25, .25), sp = R.range(60, 100),
                         life = R.range(.55, .85);
            balls.push_back({std::cos(a) * 2, CY + std::sin(a) * 2, std::cos(a) * sp, std::sin(a) * sp * .7 - 10,
                             R.range(4, 6), life, life});
        }
        const int n = o.spawn.coins;
        coins.reserve(size_t(n));
        for (int i = 0; i < n; ++i) {
            const double u = n > 1 ? double(i) / (n - 1) - .5 : 0, a = -PI / 2 + u * 1.6 + R.range(-.1, .1),
                         sp = R.range(230, 300);
            coins.push_back({R.range(-3, 3), CY, std::cos(a) * sp * .4, std::sin(a) * sp, R.range(0, TAU), spinSpeed(),
                             false, 0, 2.15 + i * .07 + R.range(0, .1), R.range(1.2, 1.9), R.range(.04, .12)});
        }
        for (int i = 0; i < 8; ++i) {
            const double a = R.range(0, TAU), sp = R.range(50, 120);
            emit({ParticleKind::Star, 0, CY, std::cos(a) * sp, std::sin(a) * sp - 20, R.range(.3, .6),
                  R.pick<uint32_t>(std::array{WHITE, C[6], C[5]}), R.range(.8, 1.8)});
        }
        lights.add(0, CY, 44, C[4], .7, .4);
        lights.add(0, CY, 20, WHITE, .7, .1);
    }
    double spinSpeed() {
        const double magnitude = R.range(9, 15);
        return magnitude * R.sign();
    }
    void emit(const Emission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size, e.delay); }
    void drawCoin(Paint& p, double x, double y, double phase) const {
        const double c = std::cos(phase), sx = std::abs(c);
        if (sx < .28) {
            for (int dy = -3; dy <= 3; ++dy) p.px(x, y + dy, std::abs(dy) == 3 ? C[1] : dy < 0 ? C[6] : C[5]);
            if (sx > .12)
                for (int dy = -2; dy <= 2; ++dy) p.px(x + (c > 0 ? 1 : -1), y + dy, C[3]);
            return;
        }
        const double half = 4 * sx;
        for (int px = int(std::floor(x - half)); px < std::ceil(x + half); ++px) {
            int col = int(std::floor((px + .5 - x) / sx + 4));
            if (col < 0 || col > 7) continue;
            if (c < 0) col = 7 - col;
            for (size_t row = 0; row < 8; ++row) {
                const auto ch = COIN[row][size_t(col)];
                if (ch != '.') p.px(px, jsRound(y - 4 + double(row)), PAL[static_cast<unsigned char>(ch)]);
            }
        }
    }
    void update(double dt) override {
        t += dt;
        for (size_t i = balls.size(); i > 0; --i) {
            auto& b = balls[i - 1];
            if ((b.life -= dt) <= 0) {
                balls.erase(balls.begin() + std::ptrdiff_t(i - 1));
                continue;
            }
            b.vx -= b.vx * 7 * dt;
            b.vy -= b.vy * 7 * dt + 26 * dt;
            b.x += b.vx * dt;
            b.y += b.vy * dt;
        }
        for (auto& c : coins) {
            if (!c.gone && t >= c.out) {
                c.gone = true;
                for (int j = 0; j < 5; ++j) {
                    const double a = R.range(-PI, 0), sp = R.range(25, 60);
                    emit({ParticleKind::Star, c.x, -3, std::cos(a) * sp, std::sin(a) * sp, R.range(.3, .55),
                          R.pick<uint32_t>(std::array{WHITE, C[6], C[5]}), R.range(.7, 1.4)});
                }
                lights.add(c.x, -3, 14, C[5], .6, .2);
            }
            if (t < c.born || t > c.out || c.rest) continue;
            c.vy += 820 * dt;
            c.vx -= c.vx * .4 * dt;
            c.x += c.vx * dt;
            c.y += c.vy * dt;
            c.phase += c.spin * dt;
            if (c.y >= -1.5 && c.vy > 0) {
                c.y = -1.5;
                ++c.hits;
                emit({ParticleKind::Star, c.x, -3, 0, -8, .25, WHITE, c.hits == 1 ? 2.2 : 1.4});
                for (int j = 0; j < 2; ++j)
                    emit({ParticleKind::Dust, c.x + R.range(-1, 1), -.5, R.signedRange(8, 24), -R.range(2, 8),
                          R.range(.25, .45), SM[1], R.range(1, 1.8)});
                lights.add(c.x, -3, 12, C[5], .5, .15);
                if (c.vy > 110) {
                    c.vy *= -.45;
                    c.vx *= .7;
                    c.spin = c.spin * .8 + R.range(-2, 2);
                } else {
                    c.rest = true;
                    c.vx = c.vy = 0;
                    c.restAt    = t;
                }
            }
        }
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        for (const auto& c : coins)
            if (c.rest && t < c.out) p.glow(c.x, -1, 7, C[4], .18);
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        for (int layer : {0, 1, 2})
            for (const auto& b : balls) {
                const double age = b.max - b.life, k = b.life / b.max,
                             r = b.radius * (.6 + .6 * ease::outCubic(clamp(age / .2))) * (k < .35 ? k / .35 : 1);
                if (r < .6) continue;
                if (layer == 0)
                    p.disc(b.x + r * .15, b.y + r * .2, r, SM[1], .8);
                else if (layer == 1)
                    p.disc(b.x - r * .06, b.y - r * .08, r * .86, b.y > CY + 2 ? SM[1] : SM[2], .85);
                else if (b.y < CY + 2)
                    p.disc(b.x - r * .28, b.y - r * .34, r * .4, SM[3], .85);
            }
        if (t < .07) {
            const double k = t / .07;
            p.circle(0, CY, 8 + 2 * k, C[6]);
            p.circle(0, CY, 6 + 2 * k, WHITE);
            flare(p, 0, CY, 16 * (1 - k * .5), 1 - k, C);
        }
        for (const auto& c : coins) {
            if (t < c.born || t > c.out + .12) continue;
            if (t > c.out) {
                const double k = (t - c.out) / .12;
                p.sparkle(c.x, -2, jsRound(5 * (1 - k)), WHITE, C[5], .5);
                p.ring(c.x, -3, 2 + 5 * ease::outCubic(k), 1, C[6], 1.1 - k);
                continue;
            }
            if (c.rest) {
                p.sprite(FLAT, PAL, jsRound(c.x) - 3, -3);
                p.glow(c.x, -2, 6, C[5], .25);
                const double g = std::fmod(t - c.restAt + c.glint, .95) / .95;
                if (g < .2) p.sparkle(c.x - 1, -3, jsRound(3 * std::sin((g / .2) * PI)), WHITE, C[5], .4);
            } else {
                drawCoin(p, c.x, c.y, c.phase);
                p.glow(c.x, c.y, 8, C[4], .3);
                if (std::abs(std::cos(c.phase)) > .96) p.px(c.x - 1, c.y - 2, WHITE);
            }
        }
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& coinDropDescriptor() {
    static const std::array<Option, 2> options{{{"smoke", 1, 64, false, 4, &PixelFxSpawnSettings::smoke},
                                                {"coins", 0, 64, false, 0, &PixelFxSpawnSettings::coins}}};
    static const Descriptor            d{
        {PixelFxKind::CoinDrop, "spawn/coin-drop", 160, 88, 80, 78, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed        = 23;
            s.spawn.coins = 7;
            s.palette     = {hex(0x2a1606), hex(0x5c3208), hex(0x9a5a0c), hex(0xd48c14),
                             hex(0xf5b82a), hex(0xffe066), hex(0xfff6c4), hex(0xffffff)};
            s.spawn.smoke = {hex(0x3a3450), hex(0x6c6688), hex(0xa8a2c0), hex(0xdcd8ea)};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<CoinDrop>(s); }};
    return d;
}
}  // namespace eve::stylize::pixelfx
