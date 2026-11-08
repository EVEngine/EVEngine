// eve-hot-path
// Native adaptation of pixelfx pickups/coin-spin.js and _lib.js coin3d, MIT.
// Copyright (c) 2026 Patch and Hugo Duprez; see LICENSE.txt.
#include "stylize/pixelfx/Descriptor.h"
#include "stylize/pixelfx/Pickup.h"
namespace eve::stylize::pixelfx {
namespace {
constexpr double D = 1.6, R = 7, TH = 3, HOVER = 17;
double           sign(double x) { return x == 0 ? 0 : x < 0 ? -1 : 1; }
double           coin3d(pickup::Layer<32, 32>& L, Paint& p, double x, double y, double r, double th, double theta,
                        std::span<const uint32_t> C) {
    const double ct = std::cos(theta), st = std::sin(theta), sg = ct < 0 ? -1 : 1, act = std::abs(ct),
                 ast = std::abs(st);
    const double xc = th / 2 * st * sg, nfx = st * sg, Lx = -.55, Ly = .45, Lz = .7, Ln = jsHypot3(Lx, Ly, Lz);
    const double lam = (nfx * Lx + act * Lz) / Ln, Hx = Lx / Ln, Hz = Lz / Ln + 1, Hn = jsHypot2(Hx, Hz);
    const int    base = int(clamp(jsRound(2 + lam * 3.2), 2, 5));
    const double X = jsRound(x), Y = jsRound(y), B = std::ceil(r + th) + 2;
    double       flash = 0;
    L.begin(X - B, Y - B);
    L.fill(X - B, Y - B, X + B, Y + B, [&](double px, double py) -> uint32_t {
        const double sx = px - X, sy = -(py - Y);
        if (std::abs(sy) > r) return 0;
        const double v = sy / r, s = std::sqrt(1 - v * v), hw = r * s * act, xr = -(st < 0 ? -1 : 1) * s * r * ct,
                     rw = th / 2 * ast;
        if (std::abs(sx - xc) <= hw && act > .08) {
            const double fx = (sx - xc) / act, fy = sy, d = jsHypot2(fx, fy) / r;
            const double tx  = nfx + fx / r * .3 * act,
                         sp  = std::pow(std::max(0.0, (tx * Hx + act * Hz) / Hn / jsHypot2(tx, act)), 130);
            flash            = std::max(flash, sp);
            const double lit = (-fx * .6 + fy * .8) / std::max(.5, jsHypot2(fx, fy));
            int          k;
            if (d > .76)
                k = base + (lit > .75 ? 2 : lit > .3 ? 1 : lit < -.35 ? -1 : 0);
            else {
                const double q = (std::abs(fx) + std::abs(fy)) / (r * .52);
                k              = q >= 1               ? base - 1
                                 : q < .45            ? base + 1
                                 : fx <= 0 && fy >= 0 ? base + 1
                                 : fx > 0 && fy < 0   ? base - 2
                                                      : base;
            }
            if (sp > .5) return sp > .8 ? WHITE : C[size_t(std::min(7, k + 2))];
            return C[size_t(std::clamp(k, 2, 6))];
        }
        if (std::abs(sx - xr) <= rw + .05) {
            const double cphi = xr < 0 ? -s : s, l = (cphi * ct * Lx + (sy / r) * Ly + (-cphi * st) * Lz) / Ln;
            const double groove = std::fmod(std::floor((std::asin(sy / r) + PI) * 9), 2);
            const bool   edge   = std::abs(sx - xr) > rw - .6 && sign(sx - xr) == sign(xc - xr);
            return C[size_t(clamp(jsRound(2.6 + l * 2.6) - groove + (edge ? 1 : 0), 1, 6))];
        }
        return 0;
    });
    L.outline(C[1]);
    L.draw(p);
    return flash;
}
void back(Paint& p, double t, const PixelFxSettings&) {
    const double ph = t / D, th = ph * TAU, y = -HOVER - 2 * std::sin(ph * TAU);
    pickup::blobShadow(p, 0, 2.5 + 4.5 * std::abs(std::cos(th)) + 1.5 * std::abs(std::sin(th)),
                       .85 - (-y - HOVER + 2) * .06);
}
void front(Paint& p, double t, const PixelFxSettings& o) {
    const auto&  C  = o.palette;
    const double ph = t / D, th = ph * TAU, y = -HOVER - 2 * std::sin(ph * TAU), facing = std::abs(std::cos(th));
    p.glow(0, y, 20, C[4], .3 + .12 * facing);
    pickup::Layer<32, 32> L;
    const double          flash = coin3d(L, p, 0, y, R, TH, th, C);
    p.glow(-1, y - 1, 12, C[6], .35 * flash);
    if (flash > .3) pickup::glint(p, -4, y - 4, 1 + 3.2 * flash, C[6], .5 * flash);
    for (uint32_t i = 0; i < 5; ++i) {
        const double life = .35, age = wrap(t - double(i) / 5 * D - rand({i, o.seed, 3}) * .08, D);
        if (age > life) continue;
        const double a = rand({i, o.seed, 1}) * TAU + i * 2.1, d = 10 + rand({i, o.seed, 2}) * 5,
                     k = std::sin(age / life * PI);
        pickup::glint(p, std::cos(a) * d, y + std::sin(a) * d * .85 - age * 8, jsRound(2 * k), i % 2 ? C[6] : WHITE,
                      .35 * k);
    }
}
}  // namespace
const Descriptor& coinSpinDescriptor() {
    static const Descriptor d{{PixelFxKind::CoinSpin, "pickups/coin-spin", 56, 56, 28, 48, D, true},
                              8,
                              [](PixelFxSettings& s) {
                                  s.seed    = 3;
                                  s.palette = {hex(0x1e1206), hex(0x4a2c08), hex(0x86520c), hex(0xc48412),
                                               hex(0xf0b424), hex(0xffe066), hex(0xfff6c4), WHITE};
                              },
                              front,
                              {},
                              nullptr,
                              back};
    return d;
}
}  // namespace eve::stylize::pixelfx
