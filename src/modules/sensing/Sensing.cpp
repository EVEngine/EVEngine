#include "sensing/Sensing.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <simplesquirrel/simplesquirrel.hpp>
#include <sstream>
#include "common/Json.h"

namespace eve::sensing {
namespace {
std::set<std::string> csv(const std::string& s) {
    std::set<std::string> out;
    std::stringstream     in(s);
    std::string           v;
    while (std::getline(in, v, ','))
        if (!v.empty()) out.insert(v);
    return out;
}
std::string quote(const std::string& s) {
    std::ostringstream o;
    o << '"';
    for (char c : s) {
        if (c == '"' || c == '\\') o << '\\';
        o << c;
    }
    return o.str() + '"';
}
std::string join(const std::set<std::string>& s) {
    std::string o;
    for (auto& v : s) {
        if (!o.empty()) o += ',';
        o += v;
    }
    return o;
}
bool finite(float v) { return std::isfinite(v); }
}  // namespace
bool SensingWorld::upsert(const std::string& id, float x, float y, const std::string& f, const std::string& t,
                          const std::string& v) {
    lastError_.clear();
    if (id.empty() || !finite(x) || !finite(y)) {
        lastError_ = "invalid subject";
        return false;
    }
    subjects_[id] = {id, x, y, f, csv(t), csv(v)};
    return true;
}
bool SensingWorld::remove(const std::string& id) {
    results_.clear();
    return subjects_.erase(id) > 0;
}
bool SensingWorld::accepts(const Subject& s, const std::set<std::string>& req, const std::set<std::string>& exc,
                           const std::set<std::string>& incF, const std::set<std::string>& excF,
                           const std::string& vis) const {
    for (auto& t : req)
        if (!s.tags.count(t)) return false;
    for (auto& t : exc)
        if (s.tags.count(t)) return false;
    if (!incF.empty() && !incF.count(s.faction)) return false;
    if (excF.count(s.faction)) return false;
    return vis.empty() || s.visibleTo.count(vis);
}
int SensingWorld::finish(float x, float y, int limit) {
    for (auto& r : results_) r.distance = std::hypot(r.x - x, r.y - y);
    std::sort(results_.begin(), results_.end(),
              [](auto& a, auto& b) { return a.distance != b.distance ? a.distance < b.distance : a.id < b.id; });
    if (limit >= 0 && size_t(limit) < results_.size()) results_.resize(size_t(limit));
    return int(results_.size());
}
int SensingWorld::circle(float x, float y, float radius, const std::string& r, const std::string& e,
                         const std::string& i, const std::string& ef, const std::string& v, int limit) {
    results_.clear();
    lastError_.clear();
    if (!finite(x) || !finite(y) || !finite(radius) || radius < 0 || limit < 0) {
        lastError_ = "invalid query";
        return 0;
    }
    auto R = csv(r), E = csv(e), I = csv(i), F = csv(ef);
    for (auto& [id, s] : subjects_)
        if ((s.x - x) * (s.x - x) + (s.y - y) * (s.y - y) <= radius * radius && accepts(s, R, E, I, F, v))
            results_.push_back({id, s.x, s.y, 0});
    return finish(x, y, limit);
}
int SensingWorld::box(float a, float b, float c, float d, const std::string& r, const std::string& e,
                      const std::string& i, const std::string& ef, const std::string& v, int limit) {
    results_.clear();
    lastError_.clear();
    if (!finite(a) || !finite(b) || !finite(c) || !finite(d) || a > c || b > d || limit < 0) {
        lastError_ = "invalid query";
        return 0;
    }
    auto  R = csv(r), E = csv(e), I = csv(i), F = csv(ef);
    float x = (a + c) / 2, y = (b + d) / 2;
    for (auto& [id, s] : subjects_)
        if (s.x >= a && s.x <= c && s.y >= b && s.y <= d && accepts(s, R, E, I, F, v))
            results_.push_back({id, s.x, s.y, 0});
    return finish(x, y, limit);
}
const Candidate* SensingWorld::resultAt(int i) const {
    return i >= 0 && size_t(i) < results_.size() ? &results_[size_t(i)] : nullptr;
}
std::string SensingWorld::snapshotJson() const {
    std::ostringstream o;
    o << "{\"version\":1,\"subjects\":[";
    bool first = true;
    for (auto& [id, s] : subjects_) {
        if (!first) o << ',';
        first = false;
        o << "{\"id\":" << quote(id) << ",\"x\":" << std::setprecision(9) << s.x << ",\"y\":" << s.y
          << ",\"faction\":" << quote(s.faction) << ",\"tags\":" << quote(join(s.tags))
          << ",\"visibleTo\":" << quote(join(s.visibleTo)) << '}';
    }
    return o.str() + "]}";
}
bool SensingWorld::restoreJson(const std::string& j) {
    lastError_.clear();
    std::string err;
    auto        d = eve::json::Document::parse(j, &err);
    if (!d.valid() || !d.root().isObject() || d.root().getInt("version") != 1 || !d.root().get("subjects").isArray()) {
        lastError_ = err.empty() ? "invalid snapshot" : err;
        return false;
    }
    SensingWorld next;
    auto         a = d.root().get("subjects");
    for (size_t n = 0; n < a.size(); ++n) {
        auto v = a.at(n);
        if (!v.isObject() || !next.upsert(v.getString("id"), float(v.getDouble("x")), float(v.getDouble("y")),
                                          v.getString("faction"), v.getString("tags"), v.getString("visibleTo"))) {
            lastError_ = "invalid snapshot subject";
            return false;
        }
    }
    subjects_ = std::move(next.subjects_);
    results_.clear();
    return true;
}
SensingWorld* Sensing::newWorld() {
    worlds_.push_back(std::make_unique<SensingWorld>());
    return worlds_.back().get();
}
Module_IMPL(Sensing, new Sensing());
void Sensing::expose(ssq::Table& t) {
    auto c = t.addClass<Candidate>("SensingCandidate", std::function<Candidate*()>([]() { return nullptr; }), false);
    c.addFunc("getId", [](Candidate* v) { return v ? v->id : std::string{}; });
    c.addFunc("getX", [](Candidate* v) { return v ? v->x : 0.f; });
    c.addFunc("getY", [](Candidate* v) { return v ? v->y : 0.f; });
    c.addFunc("getDistance", [](Candidate* v) { return v ? v->distance : 0.f; });
    auto w = t.addClass<SensingWorld>("SensingWorld", std::function<SensingWorld*()>([]() { return nullptr; }), false);
    w.addFunc("upsert", &SensingWorld::upsert);
    w.addFunc("remove", &SensingWorld::remove);
    w.addFunc("circle", &SensingWorld::circle);
    w.addFunc("box", &SensingWorld::box);
    w.addFunc("resultAt", [](SensingWorld* v, int i) -> Candidate* {
        return v ? const_cast<Candidate*>(v->resultAt(i)) : nullptr;
    });
    w.addFunc("snapshotJson", &SensingWorld::snapshotJson);
    w.addFunc("restoreJson", &SensingWorld::restoreJson);
    w.addFunc("lastError", &SensingWorld::lastError);
    auto cls = t.addClass(name, Sensing::create, false);
    expose(cls);
}
void Sensing::expose(ssq::Class& c) {
    c.addFunc("getName", &Sensing::getName);
    c.addFunc("newWorld", &Sensing::newWorld);
}
}  // namespace eve::sensing
