#include "steering/Steering.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <simplesquirrel/simplesquirrel.hpp>
#include <sstream>
#include <vector>
namespace eve::steering {
namespace {
bool ok(float v) { return std::isfinite(v); }
Vec2 scaled(float x, float y, float m) {
    float l = std::hypot(x, y);
    return l > 0 && ok(l) && ok(m) && m > 0 ? Vec2{x / l * m, y / l * m} : Vec2{};
}
std::vector<Vec2> parse(const std::string& s) {
    std::vector<Vec2> o;
    std::stringstream in(s);
    std::string       p;
    while (std::getline(in, p, ',')) {
        auto c = p.find(':');
        if (c == std::string::npos) continue;
        try {
            float x = std::stof(p.substr(0, c)), y = std::stof(p.substr(c + 1));
            if (ok(x) && ok(y)) o.push_back({x, y});
        } catch (...) {
        }
    }
    return o;
}
std::string json(Vec2 v) {
    std::ostringstream o;
    o << std::setprecision(9) << "{\"x\":" << v.x << ",\"y\":" << v.y << '}';
    return o.str();
}
}  // namespace
Vec2 Steering::seek(float x, float y, float a, float b, float m) {
    return ok(x) && ok(y) && ok(a) && ok(b) ? scaled(a - x, b - y, m) : Vec2{};
}
Vec2 Steering::flee(float x, float y, float a, float b, float m) { return seek(a, b, x, y, m); }
Vec2 Steering::arrive(float x, float y, float a, float b, float m, float slow, float stop) {
    if (!ok(slow) || !ok(stop) || slow <= stop || stop < 0) return {};
    float d = std::hypot(a - x, b - y);
    if (d <= stop) return {};
    return scaled(a - x, b - y, m * std::min(1.f, (d - stop) / (slow - stop)));
}
Vec2 Steering::separation(float x, float y, const std::string& n, float r, float m) {
    if (r <= 0 || m <= 0) return {};
    Vec2 sum{};
    for (auto p : parse(n)) {
        float dx = x - p.x, dy = y - p.y, d = std::hypot(dx, dy);
        if (d > 0 && d < r) {
            float w = (r - d) / (r * d);
            sum.x += dx * w;
            sum.y += dy * w;
        }
    }
    float l = std::hypot(sum.x, sum.y);
    return l > m ? scaled(sum.x, sum.y, m) : sum;
}
int Steering::pathTarget(float x, float y, const std::string& p, int cur, float tol) {
    auto v = parse(p);
    if (v.empty()) return -1;
    cur = std::clamp(cur, 0, int(v.size() - 1));
    while (cur + 1 < int(v.size()) && std::hypot(v[cur].x - x, v[cur].y - y) <= std::max(0.f, tol)) ++cur;
    return cur;
}
Vec2 Steering::avoid(float x, float y, float vx, float vy, float ox, float oy, float radius, float ahead, float m) {
    if (radius <= 0 || ahead < 0 || m <= 0) return {};
    Vec2  dir = scaled(vx, vy, ahead);
    float px = x + dir.x, py = y + dir.y;
    if (std::hypot(px - ox, py - oy) > radius) return {};
    return scaled(px - ox, py - oy, m);
}
std::string Steering::seekJson(float a, float b, float c, float d, float e) { return json(seek(a, b, c, d, e)); }
std::string Steering::fleeJson(float a, float b, float c, float d, float e) { return json(flee(a, b, c, d, e)); }
std::string Steering::arriveJson(float a, float b, float c, float d, float e, float f, float g) {
    return json(arrive(a, b, c, d, e, f, g));
}
std::string Steering::separationJson(float a, float b, const std::string& c, float d, float e) {
    return json(separation(a, b, c, d, e));
}
std::string Steering::avoidJson(float a, float b, float c, float d, float e, float f, float g, float h, float i) {
    return json(avoid(a, b, c, d, e, f, g, h, i));
}
Module_IMPL(Steering, new Steering());
void Steering::expose(ssq::Table& t) {
    auto c = t.addClass(name, Steering::create, false);
    expose(c);
}
void Steering::expose(ssq::Class& c) {
    c.addFunc("getName", &Steering::getName);
    c.addFunc("seek", [](Steering*, float a, float b, float d, float e, float f) { return seekJson(a, b, d, e, f); });
    c.addFunc("flee", [](Steering*, float a, float b, float d, float e, float f) { return fleeJson(a, b, d, e, f); });
    c.addFunc("arrive", [](Steering*, float a, float b, float d, float e, float f, float g, float h) {
        return arriveJson(a, b, d, e, f, g, h);
    });
    c.addFunc("separation", [](Steering*, float a, float b, const std::string& d, float e, float f) {
        return separationJson(a, b, d, e, f);
    });
    c.addFunc("pathTarget", [](Steering*, float a, float b, const std::string& d, int e, float f) {
        return pathTarget(a, b, d, e, f);
    });
    c.addFunc("avoid", [](Steering*, float a, float b, float d, float e, float f, float g, float h, float i, float j) {
        return avoidJson(a, b, d, e, f, g, h, i, j);
    });
}
}  // namespace eve::steering
