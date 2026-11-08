// eve-hot-path
// Native adaptation of pixelfx transitions/shatter-wipe.js, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt and NOTICE.txt.
#include "common/Assert.h"
#include "stylize/pixelfx/Runtime.h"
#include "stylize/pixelfx/ScreenGeometry.h"
#include "stylize/pixelfx/Simulation.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double LAND = .55, GP = (2. * screen::H) / (LAND * LAND), REST = .3, CRACK = 1.32, BREAK = 1.62, WAVE = 520;
struct Bounce {
    double t0, v, T;
};
const std::array<Bounce, 3> BOUNCES = [] {
    std::array<Bounce, 3> out{};
    double                t0 = LAND, v = GP * LAND * REST;
    for (auto& b : out) {
        b = {t0, v, (2 * v) / GP};
        t0 += (2 * v) / GP;
        v *= REST;
    }
    return out;
}();
double paneY(double t) {
    if (t < LAND) return -screen::H + .5 * GP * t * t;
    for (const auto& b : BOUNCES)
        if (t < b.t0 + b.T) return -(b.v * (t - b.t0) - .5 * GP * std::pow(t - b.t0, 2));
    return 0;
}
struct Crack {
    int    x, y;
    double t0;
};
struct Cell {
    std::vector<Point> pts, lp;
    double             cx, cy, d, r, tb;
};
struct ShatterGeometry {
    double             Ix, Iy;
    std::vector<Crack> cracks;
    std::vector<Cell>  cells;
    uint32_t           rngState;
    explicit ShatterGeometry(uint32_t seed) {
        Rng R(seed);
        Ix                                 = screen::W * R.range(.4, .6);
        Iy                                 = screen::H * R.range(.36, .54);
        constexpr int                   NR = 13;
        constexpr std::array<double, 9> RING{0, 9, 21, 37, 58, 86, 122, 168, 232};
        std::array<double, NR>          ang{}, speed{};
        for (int i = 0; i < NR; ++i) ang[size_t(i)] = ((i + R.range(-.3, .3)) / NR) * TAU;
        for (auto& v : speed) v = R.range(800, 1300);
        std::array<std::array<Point, 9>, NR> V{};
        for (int i = 0; i < NR; ++i)
            for (size_t j = 0; j < RING.size(); ++j) {
                if (!j) {
                    V[size_t(i)][j] = {Ix, Iy};
                    continue;
                }
                const double rr = RING[j] * R.range(.84, 1.16), aa = ang[size_t(i)] + R.range(-.07, .07);
                V[size_t(i)][j] = {Ix + std::cos(aa) * rr, Iy + std::sin(aa) * rr};
            }
        const auto jag = [&](Point A, Point B) {
            const double       dx = B.x - A.x, dy = B.y - A.y, L0 = jsHypot2(dx, dy), L = L0 ? L0 : 1;
            const int          n = L > 18 ? 2 : 1;
            std::vector<Point> out{A};
            out.reserve(4);
            for (int k = 1; k <= n; ++k) {
                const double f = double(k) / (n + 1) + R.range(-.08, .08), off = R.range(-1, 1) * std::min(3., L * .07);
                out.push_back({A.x + dx * f - (dy / L) * off, A.y + dy * f + (dx / L) * off});
            }
            out.push_back(B);
            return out;
        };
        std::array<std::array<std::vector<Point>, 8>, NR> rad{};
        std::array<std::array<std::vector<Point>, 9>, NR> ring{};
        for (size_t i = 0; i < NR; ++i)
            for (size_t j = 0; j < 8; ++j) rad[i][j] = jag(V[i][j], V[i][j + 1]);
        for (size_t i = 0; i < NR; ++i)
            for (size_t j = 1; j < 9; ++j) ring[i][j] = jag(V[i][j], V[(i + 1) % NR][j]);
        std::array<std::array<bool, 9>, NR> keep{};
        for (auto& row : keep)
            for (size_t j = 0; j < 9; ++j) row[j] = j <= 2 || j == 8 || R.isChance(.78 - .07 * j);
        const auto reach = [&](size_t i, double x, double y) { return CRACK + jsHypot2(x - Ix, y - Iy) / speed[i]; };
        const auto trace = [&](const std::vector<Point>& pts, auto when) {
            for (size_t k = 1; k < pts.size(); ++k) {
                const auto a = pts[k - 1], b = pts[k];
                const int  n = int(std::ceil(jsHypot2(b.x - a.x, b.y - a.y) * 2));
                for (int s = 0; s < n; ++s) {
                    const double x = a.x + ((b.x - a.x) * s) / n, y = a.y + ((b.y - a.y) * s) / n;
                    if (x >= 0 && y >= 0 && x < screen::W && y < screen::H)
                        cracks.push_back({int(std::floor(x)), int(std::floor(y)), when(x, y)});
                }
            }
        };
        for (size_t i = 0; i < NR; ++i)
            for (const auto& e : rad[i]) trace(e, [&](double x, double y) { return reach(i, x, y); });
        for (size_t i = 0; i < NR; ++i)
            for (size_t j = 1; j < 8; ++j) {
                if (!keep[i][j]) continue;
                const size_t i2 = (i + 1) % NR;
                const auto&  e  = ring[i][j];
                const double start =
                    std::max(reach(i, V[i][j].x, V[i][j].y), reach(i2, V[i2][j].x, V[i2][j].y)) + R.range(.02, .07);
                const auto a = e[0];
                trace(e, [&](double x, double y) { return start + jsHypot2(x - a.x, y - a.y) / 420; });
            }
        std::stable_sort(cracks.begin(), cracks.end(), [](const Crack& a, const Crack& b) { return a.t0 < b.t0; });
        cells.reserve(NR * 8);
        for (size_t i = 0; i < NR; ++i)
            for (size_t j = 0; j < 8;) {
                size_t jb = j;
                while (!keep[i][jb + 1]) ++jb;
                const size_t i2   = (i + 1) % NR;
                const auto   side = [&](size_t r) {
                    std::vector<Point> out;
                    for (size_t k = j; k <= jb; ++k)
                        out.insert(out.end(), rad[r][k].begin() + (k == j ? 0 : 1), rad[r][k].end());
                    return out;
                };
                auto        pts   = side(i);
                const auto& outer = ring[i][jb + 1];
                pts.insert(pts.end(), outer.begin() + 1, outer.end());
                auto other = side(i2);
                std::reverse(other.begin(), other.end());
                pts.insert(pts.end(), other.begin() + 1, other.end());
                if (j) {
                    const auto& inner = ring[i][j];
                    for (size_t k = inner.size() - 1; k > 1; --k) pts.push_back(inner[k - 1]);
                } else
                    pts.pop_back();
                double x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
                for (const auto& p : pts) {
                    x0 = std::min(x0, p.x);
                    x1 = std::max(x1, p.x);
                    y0 = std::min(y0, p.y);
                    y1 = std::max(y1, p.y);
                }
                j = jb + 1;
                if (x1 < 0 || y1 < 0 || x0 >= screen::W || y0 >= screen::H) continue;
                const double       cx = (clamp(x0, 0, screen::W) + clamp(x1, 0, screen::W)) / 2,
                                   cy = (clamp(y0, 0, screen::H) + clamp(y1, 0, screen::H)) / 2,
                                   d  = jsHypot2(cx - Ix, cy - Iy);
                std::vector<Point> lp;
                lp.reserve(pts.size());
                for (const auto& p : pts) lp.push_back({p.x - cx, p.y - cy});
                EV_ASSERT(pts.size() <= 64, "Authored eight-ring shard vertex bound");
                cells.push_back({std::move(pts), std::move(lp), cx, cy, d, jsHypot2(x1 - x0, y1 - y0) / 2,
                                 BREAK + d / WAVE + R.range(0, .04)});
            }
        EV_ASSERT(cells.size() <= 104, "Authored13x8 shard bound");
        rngState = R.state;
    }
};
struct CellState {
    double cx = 0, cy = 0, vx = 0, vy = 0, a = 0, va = 0, phi = 0, vphi = 0, psi = 0, z = 0, vz = 0, g = 0;
    bool   broke = false, gone = false, edgeOn = false;
};
struct ShatterWipe final : Kernel {
    const std::shared_ptr<const ShatterGeometry> data;
    std::vector<uint32_t>                        C;
    uint32_t                                     ink;
    Rng                                          rng;
    Particles                                    ps;
    BasicBits<>                                  bits;
    Lights                                       lights;
    std::vector<CellState>                       states;
    double                                       t      = 0;
    int                                          bounce = -1;
    bool                                         struck = false, burst = false;
    ShatterWipe(const PixelFxSettings& o, std::shared_ptr<const ShatterGeometry> geometry)
        : data(std::move(geometry)), C(o.palette), ink(o.transition.color), rng(1) {
        rng.state = data->rngState;
        ps.sparks = {C[7], C[6], C[5], C[4]};
        ps.haze   = C[5];
        states.reserve(data->cells.size());
        for (const auto& c : data->cells) {
            CellState s;
            s.cx = c.cx;
            s.cy = c.cy;
            states.push_back(s);
        }
    }
    void splinters(double x, double y, int n, double vx, double vy, double spread, double sp0, double sp1) {
        for (int i = 0; i < n; ++i) {
            const double a = std::atan2(vy, vx) + rng.range(-spread, spread), sp = rng.range(sp0, sp1),
                         life = std::min(rng.range(.6, 1.2), 2.8 - .05 - t);
            if (life <= .2) continue;
            Debris b;
            b.kind            = DebrisKind::Shard;
            b.x               = x;
            b.y               = y;
            b.vx              = std::cos(a) * sp;
            b.vy              = std::sin(a) * sp;
            b.gravity         = .8;
            b.drag            = .6;
            b.life            = life;
            b.angle           = rng.range(0, TAU);
            b.angularVelocity = rng.range(-20, 20);
            b.size            = rng.range(1.5, 3.5);
            const std::array<uint32_t, 3> colors{C[7], C[6], C[5]};
            b.color = rng.pick<uint32_t>(colors);
            bits.add(std::move(b));
        }
    }
    void glint(double x, double y, double size) {
        const double vx = rng.range(-8, 8), vy = rng.range(-10, 4), life = std::min(rng.range(.3, .6), 2.8 - .05 - t);
        const auto   c = rng.isChance(.5) ? WHITE : C[6];
        ps.spawn(rng, ParticleKind::Star, x, y, vx, vy, life, c, size);
    }
    void glint(double x, double y) {
        const double size = rng.range(1, 2.4);
        glint(x, y, size);
    }
    void update(double dt) override {
        t += dt;
        const double Ix = data->Ix, Iy = data->Iy;
        while (bounce + 1 < int(BOUNCES.size()) && t >= BOUNCES[size_t(bounce + 1)].t0) {
            const double k = BOUNCES[size_t(++bounce)].v / BOUNCES[0].v;
            if (k < .05) continue;
            for (int i = 0; i < 5; ++i) lights.add(screen::W * (i + .5) / 5, screen::H - 2, 70, C[5], .5 * k, .35);
            for (int i = 0; i < int(jsRound(26 * k)); ++i) {
                const double x = rng.range(4, screen::W - 4), vx = rng.range(-.4, .4);
                splinters(x, screen::H - 1, 1, vx, -1, .5, 120 * k, 330 * k);
            }
            for (int i = 0; i < int(jsRound(9 * k)); ++i) {
                const double x = rng.range(8, screen::W - 8), y = screen::H - rng.range(2, 10);
                glint(x, y);
            }
        }
        if (!struck && t >= CRACK) {
            struck = true;
            lights.add(Ix, Iy, 90, C[5], .8, .35);
            lights.add(Ix, Iy, 34, WHITE, .9, .1);
            splinters(Ix, Iy, 16, 0, -1, PI, 60, 240);
            for (int i = 0; i < 5; ++i) {
                const double vx = rng.range(-40, 40), vy = rng.range(-40, 30), life = rng.range(.35, .6),
                             size = rng.range(2, 3.5);
                ps.spawn(rng, ParticleKind::Vapour, Ix, Iy, vx, vy, life, C[6], size);
            }
        }
        if (t > CRACK && t < BREAK && rng.isChance(.12)) {
            const auto& c = data->cracks[size_t(rng.integer(0, int(data->cracks.size()) - 1))];
            if (c.t0 < t) {
                const double size = rng.range(.6, 1.6);
                glint(c.x, c.y, size);
            }
        }
        if (!burst && t >= BREAK) {
            burst = true;
            lights.add(Ix, Iy, 170, C[4], .55, .5);
            lights.add(Ix, Iy, 60, WHITE, .8, .14);
            splinters(Ix, Iy, 46, 0, -1, PI, 90, 340);
            for (int i = 0; i < 14; ++i) {
                const double a = rng.range(0, TAU), sp = rng.range(30, 110), life = rng.range(.45, .8);
                const auto   c    = rng.isChance(.6) ? C[6] : C[5];
                const double size = rng.range(3, 6);
                ps.spawn(rng, ParticleKind::Vapour, Ix + std::cos(a) * 4, Iy + std::sin(a) * 4, std::cos(a) * sp,
                         std::sin(a) * sp, life, c, size);
            }
        }
        for (size_t i = 0; i < states.size(); ++i) {
            auto&       s = states[i];
            const auto& c = data->cells[i];
            if (!s.broke && t >= c.tb) {
                s.broke         = true;
                const double dx = s.cx - Ix, dy = s.cy - Iy, d0 = jsHypot2(dx, dy), d = d0 ? d0 : 1,
                             sp = rng.range(40, 130) * (1.15 - std::min(1., c.d / 260));
                s.vx            = (dx / d) * sp + rng.range(-15, 15);
                s.vy            = (dy / d) * sp * .6 - rng.range(20, 90);
                s.a             = 0;
                s.va            = rng.range(-2.5, 2.5) * (1 + 20 / (c.r + 10));
                s.phi           = 0;
                const int side  = rng.sign();
                s.vphi          = side * rng.range(3, 9);
                s.psi           = rng.range(0, TAU);
                s.z             = 0;
                s.vz            = rng.range(.25, .7);
                s.g             = rng.range(.85, 1.1);
                s.edgeOn        = false;
                splinters(s.cx, s.cy, c.r > 24 ? 4 : 2, s.vx, s.vy, 1.2, 60, 220);
                if (rng.isChance(.15)) {
                    const double x = s.cx + rng.range(-4, 4), y = s.cy + rng.range(-4, 4);
                    glint(x, y);
                }
            }
            if (!s.broke || s.gone) continue;
            s.vy += 950 * s.g * dt;
            s.vx -= s.vx * .3 * dt;
            s.cx += s.vx * dt;
            s.cy += s.vy * dt;
            s.a += s.va * dt;
            s.phi += s.vphi * dt;
            s.z += s.vz * dt;
            const bool on = std::abs(std::cos(s.phi)) < .2;
            if (on && !s.edgeOn && t < 2.8 - .05 - .3) {
                const double size = rng.range(1.4, 2.6);
                glint(s.cx, s.cy, size);
            }
            s.edgeOn = on;
            if (s.cy - c.r / (1 + s.z) > screen::H + 2) s.gone = true;
        }
        ps.update(dt, rng, [](double) { return INFINITY; }, [](double, double) {});
        bits.update(dt, [](double) { return INFINITY; });
        lights.update(dt);
    }
    void drawShard(Paint& p, Paint& p0, size_t index, bool still) const {
        const auto&            s    = states[index];
        const auto&            geom = data->cells[index];
        std::array<Point, 64>  moved{};
        std::span<const Point> pts    = geom.pts;
        uint32_t               face   = ink;
        bool                   sliver = false;
        double                 gl     = 0;
        if (!still) {
            const double cp = std::cos(s.phi), sp = std::sin(s.phi), ci = std::cos(s.psi), si = std::sin(s.psi),
                         ca = std::cos(s.a), sa = std::sin(s.a), k = 1 / (1 + s.z);
            for (size_t i = 0; i < geom.lp.size(); ++i) {
                const auto   q = geom.lp[i];
                const double u = q.x * ci + q.y * si, v = (-q.x * si + q.y * ci) * cp, qx = u * ci - v * si,
                             qy = u * si + v * ci;
                moved[i]        = {s.cx + (qx * ca - qy * sa) * k, s.cy + (qx * sa + qy * ca) * k};
            }
            pts              = std::span<const Point>(moved.data(), geom.lp.size());
            const double nx0 = si * sp, ny0 = -ci * sp, nz = cp, nx = nx0 * ca - ny0 * sa, ny = nx0 * sa + ny0 * ca,
                         d     = std::abs(nx * -.42 + ny * -.56 + nz * .71);
            gl                 = std::pow(d, 6);
            const double graze = std::pow(clamp(1 - std::abs(cp) / .3), 2);
            face               = mix(mix(ink, C[2], .12 + .4 * gl), C[3], .55 * graze);
            sliver             = std::abs(cp) < .2;
            if (std::abs(cp) * geom.r < 1.5) face = C[5];
        }
        screen::fillConvex(p0, pts, face);
        double a2 = 0;
        for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) a2 += pts[j].x * pts[i].y - pts[i].x * pts[j].y;
        const int sg = a2 > 0 ? 1 : -1;
        for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
            const auto   a = pts[j], b = pts[i];
            const double dx = b.x - a.x, dy = b.y - a.y, L = jsHypot2(dx, dy);
            if (L < .5) continue;
            const double mx = (dy / L) * sg, my = (-dx / L) * sg, el = mx * -.6 + my * -.8;
            if (still) {
                p.line(a.x, a.y, b.x, b.y, el > 0 ? C[5] : C[4]);
                continue;
            }
            if (el > .2 || sliver) {
                p.line(a.x - mx, a.y - my, b.x - mx, b.y - my, C[4]);
                p.line(a.x, a.y, b.x, b.y, gl > .5 || sliver ? WHITE : C[6]);
                p.glow((a.x + b.x) / 2, (a.y + b.y) / 2, std::min(12., 4 + L * .3), C[5], .12 + .3 * gl);
            } else
                p.line(a.x, a.y, b.x, b.y, el > -.3 ? C[4] : C[2]);
        }
    }
    void drawBack(Paint&) const override {}
    void draw(Paint& p0) const override {
        auto         p  = screen::clip(p0);
        const double Ix = data->Ix, Iy = data->Iy;
        if (t < BREAK) {
            const double yb = paneY(t) + screen::H;
            const int    r1 = std::min(screen::H, int(std::floor(yb)));
            if (r1 > 0) {
                screen::rect(p0, 0, 0, screen::W, r1, ink);
                const double shine = 1 - smooth(.98, 1.22, t), y0 = yb - screen::H;
                if (shine > 0) {
                    constexpr std::array<std::array<double, 3>, 3> strips{{{0, 16, .05}, {22, 3, .1}, {30, 1, .12}}};
                    for (const auto& stripe : strips)
                        for (int y = 0; y < r1; ++y) {
                            const double xc = screen::W * .18 + stripe[0] + (screen::H - (y - y0)) * .7;
                            for (int x = std::max(0, int(std::floor(xc - stripe[1] / 2)));
                                 x < std::min(screen::W, int(std::ceil(xc + stripe[1] / 2))); ++x)
                                p0.blend(x + .5, y + .5, C[6], stripe[2] * shine);
                        }
                    const int                     st = shine > .66 ? 0 : shine > .33 ? 1 : 2;
                    const std::array<uint32_t, 3> edge{C[size_t(6 - st * 2)], C[size_t(4 - st)],
                                                       C[size_t(std::max(1, 2 - st))]};
                    for (int dy = 1; dy <= 3; ++dy)
                        if (r1 - dy >= 0 && r1 - dy < screen::H)
                            screen::rect(p0, 0, r1 - dy, screen::W, 1, edge[size_t(dy - 1)]);
                    const double gx = screen::W * clamp(t / (LAND + .25)) * 1.2 - 20;
                    for (int x = 0; x < screen::W; ++x) {
                        const double k = std::max(0., 1 - std::abs(x - gx) / 28);
                        if (k > 0 && r1 - 1 < screen::H) {
                            p0.px(x, r1 - 1, k > .5 ? WHITE : C[6]);
                            p0.light(x, r1 - 1, C[6], .5 * k);
                        }
                        for (int d = 0; d < 14 && r1 + d < screen::H; ++d)
                            p0.light(x, r1 + d, C[5], .3 * shine * std::pow(1 - d / 14., 2) * (.6 + .8 * k));
                    }
                    if (t < LAND + .25 && gx > 0 && gx < screen::W && r1 - 1 < screen::H)
                        p.sparkle(gx, r1 - 1, 6, C[6], C[4], .5);
                }
            }
            if (t >= CRACK) {
                for (const auto& c : data->cracks) {
                    if (c.t0 > t) break;
                    const double age = t - c.t0;
                    if (c.x + 1 < screen::W && c.y + 1 < screen::H) p0.px(c.x + 1, c.y + 1, C[1]);
                    p0.px(c.x, c.y, age < .04 ? WHITE : age < .18 ? C[6] : C[5]);
                    if (age < .3) p0.light(c.x, c.y, C[5], .55 * (1 - age / .3));
                }
                const double u = (t - CRACK) / .22;
                if (u < 1) {
                    flare(p, Ix, Iy, 30 * std::pow(1 - u, 1.5), 1 - u, C);
                    p.glow(Ix, Iy, 50, WHITE, .7 * (1 - u));
                }
                const double w = (t - (BREAK - .07)) / .07;
                if (w > 0)
                    for (const auto& c : data->cracks) {
                        p0.px(c.x, c.y, WHITE);
                        p0.light(c.x, c.y, C[6], .4 * w);
                    }
            }
        } else {
            std::array<size_t, 104> fall{};
            size_t                  count = 0;
            for (size_t i = 0; i < states.size(); ++i)
                if (states[i].broke && !states[i].gone) {
                    size_t j = count;
                    while (j > 0 && states[fall[j - 1]].z < states[i].z) {
                        fall[j] = fall[j - 1];
                        --j;
                    }
                    fall[j] = i;
                    ++count;
                }
            for (size_t i = 0; i < states.size(); ++i)
                if (!states[i].broke) drawShard(p, p0, i, true);
            for (size_t i = 0; i < count; ++i) drawShard(p, p0, fall[i], false);
        }
        bits.draw(p);
        ps.draw(p);
        lights.draw(p);
    }
};
struct ShatterRecipe final : SimulationRecipe {
    const std::shared_ptr<const ShatterGeometry> geometry;
    explicit ShatterRecipe(const PixelFxSettings& s) : geometry(std::make_shared<const ShatterGeometry>(s.seed)) {}
    std::unique_ptr<Kernel> create(const PixelFxSettings& s) const override {
        return std::make_unique<ShatterWipe>(s, geometry);
    }
};
}  // namespace
const Descriptor& shatterWipeDescriptor() {
    static const std::array<Option, 1> options{
        {{"color", 0, 4294967295., false, 0, &PixelFxTransitionSettings::color, true}}};
    static const Descriptor d = [] {
        Descriptor v{{PixelFxKind::ShatterWipe, "transitions/shatter-wipe", 320, 180, 0, 0, 2.8, false},
                     8,
                     [](PixelFxSettings& s) {
                         s.seed             = 2;
                         s.transition.color = hex(0x0b0a10);
                         s.palette          = {hex(0x0c1430), hex(0x162c5c), hex(0x1f4f94), hex(0x2f7fcc),
                                               hex(0x58b4ec), hex(0x9bdcff), hex(0xd8f4ff), WHITE};
                     },
                     nullptr,
                     options};
        v.prepareSimulation = [](const PixelFxSettings& s) -> std::unique_ptr<const SimulationRecipe> {
            return std::make_unique<ShatterRecipe>(s);
        };
        return v;
    }();
    return d;
}
}  // namespace eve::stylize::pixelfx
