// eve-hot-path
// Native adaptation of pixelfx spawn/shatter-death.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include <map>
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/Simulation.h"
#include "stylize/pixelfx/SpawnBody.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double LOCK = .5, CRACK = 1.05, BURST = 1.4, D = 3.3;
struct IceSpike {
    double x, base, height, width, lean, at;
};
struct IcePiece {
    int                                     id;
    double                                  x, y, vx, vy, angle, angularVelocity, radius;
    std::map<std::pair<int, int>, uint32_t> pixels;
    double                                  life, max;
    bool                                    rest;
    size_t                                  count;
};
struct FloorRime {
    double x, width, born;
};
struct IceEmission {
    ParticleKind kind;
    double       x, y, vx, vy, life;
    uint32_t     color;
    double       size;
};
struct IceDebris {
    double   x, y, vx, vy, gravity, drag, bounce, life, angle, angularVelocity, size;
    uint32_t color;
};
struct ShatterDeath final : Kernel {
    const std::vector<uint32_t>     C;
    Rng                             R;
    const spawn::Body               body;
    const int                       W, H, IX, IY;
    const double                    X0, Y0;
    std::vector<int16_t>            ice;
    std::vector<uint32_t>           col, glass;
    std::vector<uint8_t>            solid;
    std::vector<IceSpike>           spikes;
    std::vector<std::vector<Point>> cracks;
    Particles                       ps;
    Bits                            bits;
    Lights                          lights;
    std::vector<IcePiece>           pieces;
    std::vector<FloorRime>          rime;
    double                          t      = 0;
    bool                            locked = false, cracked = false, burst = false;
    explicit ShatterDeath(const PixelFxSettings& o)
        : C(o.palette),
          R(o.seed),
          body(spawn::bodyOf(o.actor, C)),
          W(body.width + 2),
          H(body.height + 1),
          IX(int(jsRound(W * .48))),
          IY(int(jsRound(H * .42))),
          X0(body.x - 1),
          Y0(body.y - 1),
          ice(size_t(W) * size_t(H), -1),
          col(ice.size()),
          glass(ice.size()),
          solid(ice.size()) {
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const int mx = x - 1, my = y - 1;
                if (body.isOccupied(mx, my) ||
                    (my < body.height - 1 && (body.isOccupied(mx - 1, my) || body.isOccupied(mx + 1, my) ||
                                              body.isOccupied(mx, my - 1) || body.isOccupied(mx, my + 1))))
                    ice[size_t(y * W + x)] = 0;
            }
        std::array<Point, 22> seeds;
        for (auto& s : seeds) {
            int tries = 0;
            do {
                s.x = R.range(0, W);
                s.y = R.range(0, H);
            } while (ice[size_t(int(std::floor(s.y)) * W + int(std::floor(s.x)))] < 0 && ++tries < 40);
        }
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const size_t i = size_t(y * W + x);
                if (ice[i] < 0) continue;
                double d1 = 1e9, d2 = 1e9;
                size_t k = 0;
                for (size_t j = 0; j < seeds.size(); ++j) {
                    const double d = jsHypot2(x + .5 - seeds[j].x, (y + .5 - seeds[j].y) * .85);
                    if (d < d1) {
                        d2 = d1;
                        d1 = d;
                        k  = j;
                    } else if (d < d2)
                        d2 = d;
                }
                ice[i]              = int16_t(k);
                const auto&    seed = seeds[k];
                const double   lit  = clamp(.55 + .35 * (rand({uint32_t(k), 41u}) - .5) -
                                            .5 * ((seed.x - double(W) / 2) / W) - .55 * ((seed.y - double(H) / 2) / H));
                const auto     ownIndex = body.cellIndex(x - 1, y - 1);
                const uint32_t own      = ownIndex ? body.list[*ownIndex].color : 0;
                col[i]                  = own ? mix(own, C[4], .36) : C[3];
                glass[i]                = C[size_t(3 + int(std::min(2., std::floor(lit * 3))))];
                uint32_t sc             = 0;
                if (d2 - d1 < .42 && !own)
                    sc = lit > .5 ? C[6] : C[5];
                else if (d2 - d1 < .42)
                    col[i] = mix(col[i], C[6], .45);
                if (!isOccupied(x - 1, y) || !isOccupied(x, y - 1)) sc = C[6];
                if (!isOccupied(x + 1, y) || (!isOccupied(x, y + 1) && y < H - 1)) sc = C[3];
                if (sc) {
                    col[i]   = sc;
                    solid[i] = 1;
                }
            }
        for (int i = 0; i < 7; ++i) {
            const int x = R.integer(1, W - 2), y0 = R.integer(2, H - 3);
            int       y = y0;
            while (y > 0 && isOccupied(x, y - 1)) --y;
            if (isOccupied(x, y))
                spikes.push_back({X0 + x + .5, Y0 + y + 1, R.range(3, 6), R.range(2.5, 3.5), R.range(-2.5, 2.5),
                                  LOCK + R.range(-.02, .08)});
        }
        for (int side : {-1, 1})
            for (int i = 0; i < 2; ++i) {
                const double h = i ? R.range(5, 7) : R.range(8, 11);
                spikes.push_back({side * (i ? R.range(11, 13) : R.range(7, 9)), 0, h, i ? 3.5 : 4.5,
                                  side * h * R.range(.25, .45), R.range(.08, .2)});
            }
        for (int i = 0; i < 7; ++i) walk(IX, IY, (double(i) / 7) * TAU + R.range(-.3, .3), 20);
        ps.sparks = {WHITE, C[6], C[5], C[4]};
        ps.haze   = C[5];
    }
    bool isOccupied(int x, int y) const { return x >= 0 && y >= 0 && x < W && y < H && ice[size_t(y * W + x)] >= 0; }
    PixelFxActorVisibility actorVisibility(double time) const override {
        return time < BURST ? PixelFxActorVisibility::Visible : PixelFxActorVisibility::Hidden;
    }
    void walk(double x, double y, double angle, double length) {
        std::vector<Point> pts{{x, y}};
        for (int s = 0; s < length; ++s) {
            angle += R.range(-.5, .5);
            x += std::cos(angle) * R.range(1.4, 2.4);
            y += std::sin(angle) * R.range(1.4, 2.4);
            if (!isOccupied(int(std::floor(x)), int(std::floor(y)))) break;
            pts.push_back({x, y});
            if (pts.size() > 3 && cracks.size() < 18 && R.isChance(.15)) {
                const double delta = R.signedRange(.6, 1.1);
                walk(x, y, angle + delta, length / 2);
            }
        }
        cracks.push_back(std::move(pts));
    }
    double level() const { return H * (1 - ease::outCubic(clamp(t / LOCK))); }
    void   emit(const IceEmission& e) { ps.spawn(R, e.kind, e.x, e.y, e.vx, e.vy, e.life, e.color, e.size); }
    void   debris(const IceDebris& e) {
        Debris b;
        b.kind            = DebrisKind::Shard;
        b.x               = e.x;
        b.y               = e.y;
        b.vx              = e.vx;
        b.vy              = e.vy;
        b.gravity         = e.gravity;
        b.drag            = e.drag;
        b.bounce          = e.bounce;
        b.life            = e.life;
        b.angle           = e.angle;
        b.angularVelocity = e.angularVelocity;
        b.size            = e.size;
        b.color           = e.color;
        bits.add(std::move(b));
    }
    void shatter() {
        struct Cell {
            int      x, y;
            uint32_t color;
        };
        struct Group {
            int               key;
            std::vector<Cell> cells;
        };
        std::vector<Group>  groups;
        std::array<int, 22> indices;
        indices.fill(-1);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const size_t i = size_t(y * W + x);
                const int    k = ice[i];
                if (k < 0) continue;
                if (indices[size_t(k)] < 0) {
                    indices[size_t(k)] = int(groups.size());
                    groups.push_back({k, {}});
                }
                groups[size_t(indices[size_t(k)])].cells.push_back(
                    {x, y, solid[i] ? col[i] : mix(col[i], glass[i], .3)});
            }
        for (const auto& g : groups) {
            double cx = 0, cy = 0;
            for (const auto& c : g.cells) {
                cx += c.x + .5;
                cy += c.y + .5;
            }
            cx /= double(g.cells.size());
            cy /= double(g.cells.size());
            std::map<std::pair<int, int>, uint32_t> map;
            double                                  radius = 0;
            for (const auto& c : g.cells) {
                radius = std::max(radius, jsHypot2(c.x + .5 - cx, c.y + .5 - cy));
                map[{int(jsRound(c.x + .5 - cx)), int(jsRound(c.y + .5 - cy))}] = c.color;
            }
            radius += 1;
            const double dx = cx - IX, dy = cy - IY, hypot = jsHypot2(dx, dy), distance = hypot == 0 ? 1 : hypot,
                         sp = R.range(55, 115), life = R.range(1.3, 1.85);
            pieces.push_back({int(pieces.size()) + 1, X0 + cx, Y0 + cy, (dx / distance) * sp + R.range(-18, 18),
                              (dy / distance) * sp * .6 - R.range(110, 220), 0, R.range(-9, 9), radius, std::move(map),
                              life, life, false, g.cells.size()});
        }
        for (int i = 0; i < 64; ++i) {
            const double a = -PI / 2 + R.range(-1.45, 1.45), sp = R.range(60, 180);
            debris({X0 + IX + R.range(-6, 6), Y0 + IY + R.range(-8, 8), std::cos(a) * sp, std::sin(a) * sp, .85, .6, .3,
                    R.range(.9, 1.6), R.range(0, TAU), R.range(-20, 20), R.range(1.5, 3.2),
                    R.pick<uint32_t>(std::array{C[7], C[6], C[5]})});
        }
        for (int i = 0; i < 14; ++i) {
            const double x = R.range(-10, 10), y = -R.range(1, 20), vx = R.signedRange(20, 80);
            emit({ParticleKind::Vapour, x, y, vx, -R.range(4, 20), R.range(.6, 1.1), R.isChance(.5) ? C[6] : C[5],
                  R.range(2.5, 4.5)});
        }
        for (int i = 0; i < 16; ++i)
            emit({ParticleKind::Star, R.range(-16, 16), -R.range(4, 38), R.range(-40, 40), -R.range(10, 60),
                  R.range(.4, 1), R.pick<uint32_t>(std::array{WHITE, C[6]}), R.range(1, 2.4)});
        lights.add(0, Y0 + IY, 46, WHITE, .75, .14);
        lights.add(0, Y0 + IY, 70, C[4], .6, .5);
    }
    void update(double dt) override {
        t += dt;
        if (t < LOCK) {
            const double y = Y0 + level();
            if (R.isChance(.7))
                emit({ParticleKind::Star, R.range(-10, 10), y, R.range(-12, 12), -R.range(0, 18), R.range(.25, .5),
                      R.isChance(.5) ? WHITE : C[6], R.range(.8, 1.8)});
            if (R.isChance(.25)) {
                const double x = R.range(-8, 8), vx = R.signedRange(15, 50);
                emit({ParticleKind::Vapour, x, -1, vx, -R.range(2, 10), R.range(.6, 1), C[6], R.range(2, 3.5)});
            }
        }
        if (!locked && t >= LOCK) {
            locked = true;
            lights.add(0, -20, 54, C[4], .6, .3);
            for (int i = 0; i < 6; ++i)
                emit({ParticleKind::Star, R.range(-12, 12), -R.range(4, 36), 0, -4, R.range(.35, .7),
                      R.pick<uint32_t>(std::array{WHITE, C[6]}), R.range(1.2, 2.4)});
        }
        if (locked && t < CRACK && R.isChance(.08))
            emit({ParticleKind::Star, R.range(-11, 11), -R.range(2, 36), 0, -3, R.range(.3, .6),
                  R.pick<uint32_t>(std::array{WHITE, C[6]}), R.range(.8, 2)});
        if (!cracked && t >= CRACK) {
            cracked = true;
            for (int i = 0; i < 5; ++i) {
                const double a = R.range(0, TAU), sp = R.range(60, 150);
                debris({X0 + IX, Y0 + IY, std::cos(a) * sp, std::sin(a) * sp - 60, .8, .6, .3, R.range(.5, .9),
                        R.range(0, TAU), R.range(-20, 20), R.range(1, 2), C[6]});
            }
            lights.add(0, Y0 + IY, 24, WHITE, .7, .1);
        }
        if (!burst && t >= BURST) {
            burst = true;
            shatter();
        }
        for (size_t left = pieces.size(); left > 0; --left) {
            const size_t i = left - 1;
            auto&        q = pieces[i];
            q.life -= dt;
            if (q.life <= 0) {
                pieces.erase(pieces.begin() + std::ptrdiff_t(i));
                continue;
            }
            if (q.rest) {
                if (t < D - .9 && R.isChance(.02))
                    emit({ParticleKind::Vapour, q.x, q.y - 1, R.range(-4, 4), -R.range(4, 10), R.range(.5, .8), C[5],
                          R.range(1.5, 2.5)});
                continue;
            }
            q.vy += 900 * dt;
            q.vx -= q.vx * .5 * dt;
            q.x += q.vx * dt;
            q.y += q.vy * dt;
            q.angle += q.angularVelocity * dt;
            const double floor = -q.radius * .55;
            if (q.y > floor && q.vy > 0) {
                q.y = floor;
                if (q.vy > 90) {
                    rime.push_back({q.x, 2 + q.radius * .6, t});
                    q.vy *= -.32;
                    q.vx *= .65;
                    q.angularVelocity = q.angularVelocity * .5 + R.range(-4, 4);
                    if (q.count > 8) emit({ParticleKind::Star, q.x, q.y, 0, -6, .3, WHITE, 1.2});
                    for (int j = 0; j < 2; ++j) {
                        const double x = q.x + R.range(-2, 2), vx = R.signedRange(10, 30);
                        emit({ParticleKind::Dust, x, -.5, vx, -R.range(2, 8), R.range(.3, .5), C[5], R.range(1.2, 2)});
                    }
                } else if (std::abs(q.vx) > 12) {
                    q.vy = 0;
                    q.vx *= .9;
                    q.angularVelocity *= .85;
                } else {
                    q.rest = true;
                    q.vx = q.vy = 0;
                }
            }
        }
        bits.update(dt, [](double) { return 0.; });
        ps.update(dt, R, [](double) { return 0.; }, [](double, double) {});
        lights.update(dt);
    }
    void drawBack(Paint& p) const override {
        const double k     = smooth(.1, LOCK, t) * (1 - smooth(BURST + .6, BURST + 1.6, t)),
                     reach = 6 + 12 * smooth(0, LOCK + .3, t);
        if (k > 0)
            for (int x = int(std::floor(-reach)); x <= std::ceil(reach); ++x) {
                const double h = rand({jsUint(x), 17u}), e = 1 - std::abs(double(x)) / (reach + 1);
                p.dot(x, 0, h < .3 ? WHITE : C[6], k * std::min(1., e * 2));
                if (h < .5) p.dot(x, 1, C[5], k * e);
                if (h < .35 && e > .3) p.dot(x, -1, C[6], k * e);
            }
        if (k > 0) p.glow(0, -2, 26, C[4], .25 * k);
        for (const auto& r : rime) {
            const double a = clamp((t - r.born) / .08) * (1 - smooth(D - 1, D - .15, t));
            for (int x = int(std::floor(r.x - r.width)); x <= r.x + r.width; ++x) {
                const double h = rand({jsUint(x), 23u});
                if (h < .7) p.dot(x, 0, h < .25 ? WHITE : C[6], a);
                if (h < .3) p.dot(x, 1, C[5], a * .7);
            }
        }
        ps.draw(p, Paint::Layer::Back);
    }
    void draw(Paint& p) const override {
        if (!burst) {
            const double lv    = level(),
                         shake = t > CRACK && t < BURST
                                     ? (std::fmod(std::floor(t * 50), 2) != 0 ? 1 : -1) * (t > BURST - .15 ? 1 : .5)
                                     : 0;
            const double glint = t > LOCK ? std::fmod((t - LOCK) * .9, 1.6) : -1;
            for (int y = 0; y < H; ++y) {
                if (y < std::floor(lv)) continue;
                for (int x = 0; x < W; ++x) {
                    const size_t i = size_t(y * W + x);
                    if (ice[i] < 0) continue;
                    // Upstream typed col[] is unsigned, whereas literal/ramp colors are signed JS bitwise numbers.
                    const auto signedColor = [](uint32_t c) { return int64_t(c) - 4294967296LL; };
                    int64_t c = y == std::floor(lv) && t < LOCK ? (rand({jsUint(x), 5u}) < .6 ? -1 : signedColor(C[6]))
                                : t >= LOCK && t < LOCK + .05   ? -1
                                                                : int64_t(col[i]);
                    const double g  = std::abs((x + (H - y) * .6) / (W + H * .6) - glint);
                    const bool   on = glint >= 0 && glint <= 1;
                    if (on && g < .035)
                        c = -1;
                    else if (on && g < .07 && c != signedColor(C[3]))
                        c = signedColor(C[6]);
                    p.px(X0 + x + shake, Y0 + y, uint32_t(c));
                    if (!solid[i] && c == int64_t(col[i])) p.blend(X0 + x + shake, Y0 + y, glass[i], .3);
                }
            }
            for (int y = std::max(0, int(std::floor(lv)) - 1); y <= H; ++y)
                for (int x = -1; x <= W; ++x)
                    if (!isOccupied(x, y) &&
                        (isOccupied(x - 1, y) || isOccupied(x + 1, y) || (isOccupied(x, y + 1) && y + 1 >= lv) ||
                         isOccupied(x, y - 1)) &&
                        y < H)
                        p.px(X0 + x + shake, Y0 + y, C[0]);
            for (const auto& s : spikes) {
                const double g = ease::outBack(clamp((t - s.at) / .12), 2.4);
                if (g > 0)
                    crystal(p, s.x + shake, s.base, s.height * g, s.width, s.lean * g, C, {1, t < s.at + .04, -1});
            }
            if (cracked) {
                const double k = clamp((t - CRACK) / .25);
                for (const auto& pts : cracks) {
                    const int n = int(std::floor(double(pts.size()) * k));
                    for (int i = 0; i < n; ++i) {
                        const auto& pt    = pts[size_t(i)];
                        const bool  fresh = i > n - 3 && t - CRACK < .3;
                        p.px(X0 + pt.x + shake, Y0 + pt.y, fresh ? WHITE : C[7]);
                        if (!fresh) p.px(X0 + pt.x + shake, Y0 + pt.y + 1, C[1]);
                    }
                }
            }
            p.glow(0, -18, 30, C[4], .18 * smooth(0, LOCK, t));
            if (t > LOCK && t < LOCK + .18) p.sparkle(-4, Y0 + 8, jsRound(8 * (1 - (t - LOCK) / .18)), WHITE, C[5], .6);
        } else if (t < BURST + .08) {
            const double k = (t - BURST) / .08;
            p.circle(X0 + IX, Y0 + IY, 10 * (1 - k) + 3, WHITE);
        }
        for (const auto& q : pieces) {
            const double ca = std::cos(q.angle), sa = std::sin(q.angle), fade = std::min(1., q.life / .5);
            const int    radius = int(std::ceil(q.radius));
            for (int py = -radius; py <= radius; ++py)
                for (int px = -radius; px <= radius; ++px) {
                    const int  ux = int(jsRound(px * ca + py * sa)), uy = int(jsRound(-px * sa + py * ca));
                    const auto it = q.pixels.find({ux, uy});
                    if (it != q.pixels.end() && it->second && rand({jsUint(ux), jsUint(uy), jsUint(q.id)}) < fade)
                        p.px(std::floor(q.x) + px, std::floor(q.y) + py, q.max - q.life < .05 ? WHITE : it->second);
                }
            if (q.count > 6) p.glow(q.x, q.y, 6, C[5], .2 * fade);
        }
        bits.draw(p);
        ps.draw(p, Paint::Layer::Front);
        lights.draw(p);
    }
};
}  // namespace
const Descriptor& shatterDeathDescriptor() {
    static const std::array<Option, 1> options{{{"actor", 0, 0, false, 0, &PixelFxSettings::actor}}};
    static const Descriptor            d{
        {PixelFxKind::ShatterDeath, "spawn/shatter-death", 172, 96, 84, 84, D, false},
        8,
        [](PixelFxSettings& s) {
            s.seed         = 14;
            s.actor.source = PixelFxActorSource::Dummy;
            s.palette      = {hex(0x0e1636), hex(0x1a2f66), hex(0x2557a3), hex(0x3488d6),
                              hex(0x5fc0f0), hex(0xa2e6ff), hex(0xdcf8ff), WHITE};
        },
        nullptr,
        options,
        [](const PixelFxSettings& s) -> std::unique_ptr<Kernel> { return std::make_unique<ShatterDeath>(s); },
        nullptr,
        nullptr,
        true};
    return d;
}
}  // namespace eve::stylize::pixelfx
