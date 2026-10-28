#include "procgen/algorithms/LSystem.h"
#include "procgen/PointSet.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace eve::procgen {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float radians(float degrees) { return degrees * (kPi / 180.f); }

float clampUnit(float value) { return std::max(0.f, std::min(1.f, value)); }

}  // namespace

LSystem::LSystem() {
    std::fill(std::begin(leafChars_), std::end(leafChars_), false);
}

void LSystem::setAxiom(const std::string& axiom) { axiom_ = axiom; }

void LSystem::addRule(char symbol, const std::string& production) {
    rules_[static_cast<unsigned char>(symbol)] = {Rule{production, 1.f}};
}

void LSystem::addRules(char symbol, const std::vector<std::string>& productions,
                       const std::vector<float>& weights) {
    auto&       target = rules_[static_cast<unsigned char>(symbol)];
    const size_t count = std::min(productions.size(), weights.size());
    target.clear();
    for (size_t i = 0; i < count; ++i) {
        const float w = i < weights.size() ? std::max(0.f, weights[i]) : 1.f;
        target.emplace_back(productions[i], w);
    }
}

void LSystem::clearRules() {
    for (auto& slot : rules_) slot.clear();
}

void LSystem::setAngle(float degrees) { angleDeg_ = degrees; }
void LSystem::setStep(float step) { step_ = std::max(0.f, step); }
void LSystem::setIterations(int iterations) { iterations_ = std::max(0, iterations); }
void LSystem::setSeed(uint32_t seed) { seed_ = seed; }

void LSystem::setInitialHeading(float x, float y, float z) {
    const float n = std::sqrt(x * x + y * y + z * z);
    if (n <= 1e-9f) return;
    heading0_ = {x / n, y / n, z / n};
}

void LSystem::setBranchRadius(float radius) { branchRadius_ = std::max(0.f, radius); }
void LSystem::setBranchRadiusFalloff(float factor) { radiusFalloff_ = clampUnit(factor); }
void LSystem::setLeafSize(float size) { leafSize_ = std::max(0.f, size); }

void LSystem::setLeafSymbols(const std::string& symbols) {
    std::fill(std::begin(leafChars_), std::end(leafChars_), false);
    for (char c : symbols) leafChars_[static_cast<unsigned char>(c)] = true;
}

void LSystem::setTropism(float x, float y, float z) { tropism_ = {x, y, z}; }

uint32_t LSystem::getSeed() const { return seed_; }
int      LSystem::getIterations() const { return iterations_; }

LSystem::Vec3 LSystem::rotate(const Vec3& v, const Vec3& axis, float angle) const {
    const float c   = std::cos(angle);
    const float s   = std::sin(angle);
    const float dot = v.x * axis.x + v.y * axis.y + v.z * axis.z;
    Vec3        cross{axis.y * v.z - axis.z * v.y, axis.z * v.x - axis.x * v.z,
                      axis.x * v.y - axis.y * v.x};
    return {v.x * c + cross.x * s + axis.x * dot * (1.f - c),
            v.y * c + cross.y * s + axis.y * dot * (1.f - c),
            v.z * c + cross.z * s + axis.z * dot * (1.f - c)};
}

void LSystem::orthonormalize(Vec3& heading, Vec3& up) const {
    // right = normalize(cross(up, heading)); then re-derive up to stay exact.
    Vec3 right{up.y * heading.z - up.z * heading.y, up.z * heading.x - up.x * heading.z,
               up.x * heading.y - up.y * heading.x};
    float n = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
    if (n <= 1e-9f) return;
    right = {right.x / n, right.y / n, right.z / n};
    // up = normalize(cross(heading, right))
    Vec3 nextUp{heading.y * right.z - heading.z * right.y,
                heading.z * right.x - heading.x * right.z,
                heading.x * right.y - heading.y * right.x};
    n = std::sqrt(nextUp.x * nextUp.x + nextUp.y * nextUp.y + nextUp.z * nextUp.z);
    if (n > 1e-9f) up = {nextUp.x / n, nextUp.y / n, nextUp.z / n};
}

std::string LSystem::derive() const {
    std::string word = axiom_;
    std::mt19937      rng(seed_);
    std::uniform_real_distribution<float> unit(0.f, 1.f);
    for (int iter = 0; iter < iterations_; ++iter) {
        std::string next;
        next.reserve(word.size() * 2 + 16);
        for (char ch : word) {
            const std::vector<Rule>& ruleSet = rules_[static_cast<unsigned char>(ch)];
            if (ruleSet.empty()) {  // no productions: keep the terminal symbol
                next.push_back(ch);
                continue;
            }
            if (ruleSet.size() == 1) {
                next += ruleSet[0].first;
                continue;
            }
            float total = 0.f;
            for (const Rule& r : ruleSet) total += r.second;
            if (total <= 0.f) {
                next += ruleSet[0].first;
                continue;
            }
            const float        pick   = total * unit(rng);
            float              acc    = 0.f;
            const std::string* chosen = &ruleSet[0].first;
            for (const Rule& r : ruleSet) {
                acc += r.second;
                if (pick < acc) {
                    chosen = &r.first;
                    break;
                }
            }
            next += *chosen;
        }
        word.swap(next);
    }
    return word;
}

void LSystem::interpret(const std::string& word, LSystemResult& out) const {
    const float angle = radians(angleDeg_);
    TurtleState t;
    t.pos     = {0.f, 0.f, 0.f};
    t.heading = heading0_;
    t.up      = {0.f, 0.f, 1.f};
    t.depth   = 0;
    orthonormalize(t.heading, t.up);

    std::vector<TurtleState> stack;

    for (char ch : word) {
        switch (ch) {
            case 'F': {
                // Slight phototropism bias toward the tropism direction.
                if (tropism_.x != 0.f || tropism_.y != 0.f || tropism_.z != 0.f) {
                    t.heading = {t.heading.x + tropism_.x * 0.02f,
                                 t.heading.y + tropism_.y * 0.02f,
                                 t.heading.z + tropism_.z * 0.02f};
                    const float n = std::sqrt(t.heading.x * t.heading.x +
                                              t.heading.y * t.heading.y + t.heading.z * t.heading.z);
                    if (n > 1e-9f) t.heading = {t.heading.x / n, t.heading.y / n, t.heading.z / n};
                    orthonormalize(t.heading, t.up);
                }
                const Vec3 start = t.pos;
                const float scale = std::pow(radiusFalloff_, float(t.depth));
                const float r0 = branchRadius_ * scale;
                t.pos = {t.pos.x + t.heading.x * step_, t.pos.y + t.heading.y * step_,
                         t.pos.z + t.heading.z * step_};
                const float r1 = branchRadius_ * std::pow(radiusFalloff_, float(t.depth + 1));
                LSystemSegment seg;
                seg.sx = start.x; seg.sy = start.y; seg.sz = start.z;
                seg.ex = t.pos.x; seg.ey = t.pos.y; seg.ez = t.pos.z;
                seg.r0 = r0; seg.r1 = r1; seg.depth = t.depth;
                out.segments.push_back(seg);
                break;
            }
            case 'f':
                t.pos = {t.pos.x + t.heading.x * step_, t.pos.y + t.heading.y * step_,
                         t.pos.z + t.heading.z * step_};
                break;
            case '+': t.heading = rotate(t.heading, t.up, angle); orthonormalize(t.heading, t.up); break;
            case '-': t.heading = rotate(t.heading, t.up, -angle); orthonormalize(t.heading, t.up); break;
            case '&': {
                Vec3 right{0.f, 0.f, 0.f};
                orthonormalize(t.heading, t.up);
                right = {t.up.y * t.heading.z - t.up.z * t.heading.y,
                         t.up.z * t.heading.x - t.up.x * t.heading.z,
                         t.up.x * t.heading.y - t.up.y * t.heading.x};
                const float n = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
                if (n > 1e-9f) right = {right.x / n, right.y / n, right.z / n};
                t.heading = rotate(t.heading, right, angle);
                orthonormalize(t.heading, t.up);
                break;
            }
            case '^': {
                Vec3 right{0.f, 0.f, 0.f};
                orthonormalize(t.heading, t.up);
                right = {t.up.y * t.heading.z - t.up.z * t.heading.y,
                         t.up.z * t.heading.x - t.up.x * t.heading.z,
                         t.up.x * t.heading.y - t.up.y * t.heading.x};
                const float n = std::sqrt(right.x * right.x + right.y * right.y + right.z * right.z);
                if (n > 1e-9f) right = {right.x / n, right.y / n, right.z / n};
                t.heading = rotate(t.heading, right, -angle);
                orthonormalize(t.heading, t.up);
                break;
            }
            case '\\': t.up = rotate(t.up, t.heading, angle); orthonormalize(t.heading, t.up); break;
            case '/':  t.up = rotate(t.up, t.heading, -angle); orthonormalize(t.heading, t.up); break;
            case '[':
                stack.push_back(t);
                t.depth += 1;
                break;
            case ']':
                if (!stack.empty()) {
                    t = stack.back();
                    stack.pop_back();
                }
                break;
            default:
                if (leafChars_[static_cast<unsigned char>(ch)]) {
                    LSystemSegment seg;
                    seg.sx = t.pos.x; seg.sy = t.pos.y; seg.sz = t.pos.z;
                    seg.ex = t.pos.x; seg.ey = t.pos.y; seg.ez = t.pos.z;
                    seg.r0 = 0.f; seg.r1 = 0.f; seg.depth = t.depth;
                    seg.leaf = true; seg.leafSize = leafSize_;
                    seg.dx = t.heading.x; seg.dy = t.heading.y; seg.dz = t.heading.z;
                    out.segments.push_back(seg);
                    out.leafCount += 1;
                }
                break;
        }
    }
}

void LSystem::generate(LSystemResult& out) const {
    out.segments.clear();
    out.leafCount = 0;
    out.derivation = derive();
    interpret(out.derivation, out);
}

void LSystem::toPointSet(PointSet& out) const {
    LSystemResult result;
    generate(result);
    for (const LSystemSegment& seg : result.segments) {
        out.add(seg.sx, seg.sy, seg.sz);
        out.add(seg.ex, seg.ey, seg.ez);
    }
}

}  // namespace eve::procgen