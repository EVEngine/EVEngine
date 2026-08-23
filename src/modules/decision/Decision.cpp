#include "decision/Decision.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <simplesquirrel/simplesquirrel.hpp>
#include <sstream>
#include "common/Json.h"
namespace eve::decision {
namespace {
std::string q(const std::string& s) {
    std::ostringstream o;
    o << '"';
    for (char c : s) {
        if (c == '"' || c == '\\') o << '\\';
        o << c;
    }
    return o.str() + '"';
}
bool        scalar(const eve::json::Value& v) { return v.isNull() || v.isBool() || v.isNumber() || v.isString(); }
std::string canon(const eve::json::Value& v) {
    if (v.isNull()) return "null";
    if (v.isBool()) return v.asBool() ? "true" : "false";
    if (v.isNumber()) return v.asString();
    return q(v.asString());
}
bool num(const std::string& s, float& v) {
    try {
        size_t n = 0;
        v        = std::stof(s, &n);
        return n == s.size() && std::isfinite(v);
    } catch (...) {
        return false;
    }
}
}  // namespace
bool DecisionContext::set(const std::string& b, const std::string& k, const std::string& j) {
    lastError_.clear();
    std::string e;
    auto        d = eve::json::Document::parse(j, &e);
    if (b.empty() || k.empty() || !d.valid() || !scalar(d.root())) {
        lastError_ = "invalid blackboard value";
        return false;
    }
    boards_[b][k] = canon(d.root());
    return true;
}
std::string DecisionContext::get(const std::string& b, const std::string& k, const std::string& f) const {
    auto i = boards_.find(b);
    if (i == boards_.end()) return f;
    auto j = i->second.find(k);
    return j == i->second.end() ? f : j->second;
}
bool DecisionContext::addTransition(const std::string& m, const std::string& f, const std::string& t,
                                    const std::string& to) {
    if (m.empty() || f.empty() || t.empty() || to.empty()) return false;
    transitions_[m][{f, t}] = to;
    return true;
}
bool DecisionContext::setState(const std::string& m, const std::string& s) {
    if (m.empty() || s.empty()) return false;
    states_[m] = s;
    return true;
}
bool DecisionContext::trigger(const std::string& m, const std::string& t) {
    auto s = states_.find(m);
    auto a = transitions_.find(m);
    if (s == states_.end() || a == transitions_.end()) return false;
    auto i = a->second.find({s->second, t});
    if (i == a->second.end()) return false;
    s->second = i->second;
    return true;
}
std::string DecisionContext::state(const std::string& m) const {
    auto i = states_.find(m);
    return i == states_.end() ? std::string{} : i->second;
}
float DecisionContext::utility(const std::string& s) {
    std::stringstream in(s);
    std::string       p;
    double            sum = 0, w = 0;
    while (std::getline(in, p, ',')) {
        auto c = p.find(':');
        if (c == std::string::npos) continue;
        float v = 0, a = 0;
        if (num(p.substr(0, c), v) && num(p.substr(c + 1), a) && a > 0) {
            sum += std::clamp(v, 0.f, 1.f) * a;
            w += a;
        }
    }
    return w ? float(sum / w) : 0.f;
}
std::string DecisionContext::choose(const std::string& s) {
    std::stringstream in(s);
    std::string       p, best;
    float             score = -1;
    while (std::getline(in, p, ';')) {
        auto c = p.find('=');
        if (c == std::string::npos || c == 0) continue;
        auto  name = p.substr(0, c);
        float v    = utility(p.substr(c + 1));
        if (v > score || (v == score && (best.empty() || name < best))) {
            score = v;
            best  = name;
        }
    }
    return best;
}
bool DecisionContext::newGrid(const std::string& n, int w, int h, float c, float x, float y) {
    if (n.empty() || w <= 0 || h <= 0 || !std::isfinite(c) || c <= 0 || size_t(w) > 10000000 / size_t(h)) return false;
    grids_[n] = {w, h, c, x, y, std::vector<float>(size_t(w) * h)};
    return true;
}
bool DecisionContext::setCell(const std::string& n, int x, int y, float v) {
    auto i = grids_.find(n);
    if (i == grids_.end() || x < 0 || y < 0 || x >= i->second.w || y >= i->second.h || !std::isfinite(v)) return false;
    i->second.values[size_t(y) * i->second.w + x] = v;
    return true;
}
bool DecisionContext::addCell(const std::string& n, int x, int y, float v) {
    auto i = grids_.find(n);
    if (i == grids_.end() || x < 0 || y < 0 || x >= i->second.w || y >= i->second.h || !std::isfinite(v)) return false;
    auto& z = i->second.values[size_t(y) * i->second.w + x];
    if (!std::isfinite(z + v)) return false;
    z += v;
    return true;
}
float DecisionContext::sample(const std::string& n, float x, float y, float f) const {
    auto i = grids_.find(n);
    if (i == grids_.end()) return f;
    int cx = int(std::floor((x - i->second.ox) / i->second.cell)),
        cy = int(std::floor((y - i->second.oy) / i->second.cell));
    return cx < 0 || cy < 0 || cx >= i->second.w || cy >= i->second.h ? f
                                                                      : i->second.values[size_t(cy) * i->second.w + cx];
}
std::string DecisionContext::snapshotJson() const {
    std::ostringstream o;
    o << std::setprecision(9) << "{\"version\":1,\"boards\":{";
    bool a = true;
    for (auto& [b, vs] : boards_) {
        if (!a) o << ',';
        a = false;
        o << q(b) << ":{";
        bool f = true;
        for (auto& [k, v] : vs) {
            if (!f) o << ',';
            f = false;
            o << q(k) << ':' << v;
        }
        o << '}';
    }
    o << "},\"states\":{";
    a = true;
    for (auto& [m, s] : states_) {
        if (!a) o << ',';
        a = false;
        o << q(m) << ':' << q(s);
    }
    o << "},\"transitions\":[";
    a = true;
    for (auto& [m, ts] : transitions_)
        for (auto& [key, to] : ts) {
            if (!a) o << ',';
            a = false;
            o << "[" << q(m) << ',' << q(key.first) << ',' << q(key.second) << ',' << q(to) << "]";
        }
    o << "],\"grids\":[";
    a = true;
    for (auto& [n, g] : grids_) {
        if (!a) o << ',';
        a = false;
        o << "{\"name\":" << q(n) << ",\"w\":" << g.w << ",\"h\":" << g.h << ",\"cell\":" << g.cell
          << ",\"ox\":" << g.ox << ",\"oy\":" << g.oy << ",\"values\":[";
        for (size_t i = 0; i < g.values.size(); ++i) {
            if (i) o << ',';
            o << g.values[i];
        }
        o << "]}";
    }
    return o.str() + "]}";
}
bool DecisionContext::restoreJson(const std::string& j) {
    lastError_.clear();
    std::string e;
    auto        d = eve::json::Document::parse(j, &e);
    if (!d.valid() || !d.root().isObject() || d.root().getInt("version") != 1) {
        lastError_ = "invalid snapshot";
        return false;
    }
    DecisionContext n;
    auto            b = d.root().get("boards");
    if (!b.isObject()) return false;
    for (auto& bn : b.keys()) {
        auto v = b.get(bn.c_str());
        if (!v.isObject()) return false;
        for (auto& k : v.keys())
            if (!n.set(bn, k, canon(v.get(k.c_str())))) return false;
    }
    auto st = d.root().get("states");
    if (!st.isObject()) return false;
    for (auto& m : st.keys())
        if (!n.setState(m, st.getString(m.c_str()))) return false;
    auto tr = d.root().get("transitions");
    if (!tr.isArray()) return false;
    for (size_t i = 0; i < tr.size(); ++i) {
        auto v = tr.at(i);
        if (!v.isArray() || v.size() != 4 ||
            !n.addTransition(v.at(0).asString(), v.at(1).asString(), v.at(2).asString(), v.at(3).asString()))
            return false;
    }
    auto gs = d.root().get("grids");
    if (!gs.isArray()) return false;
    for (size_t i = 0; i < gs.size(); ++i) {
        auto v = gs.at(i);
        int  w = v.getInt("w"), h = v.getInt("h");
        if (!n.newGrid(v.getString("name"), w, h, float(v.getDouble("cell")), float(v.getDouble("ox")),
                       float(v.getDouble("oy"))))
            return false;
        auto vals = v.get("values");
        if (!vals.isArray() || vals.size() != size_t(w) * h) return false;
        for (size_t z = 0; z < vals.size(); ++z)
            if (!n.setCell(v.getString("name"), int(z % w), int(z / w), float(vals.at(z).asDouble()))) return false;
    }
    boards_      = std::move(n.boards_);
    states_      = std::move(n.states_);
    transitions_ = std::move(n.transitions_);
    grids_       = std::move(n.grids_);
    return true;
}
DecisionContext* Decision::newContext() {
    contexts_.push_back(std::make_unique<DecisionContext>());
    return contexts_.back().get();
}
Module_IMPL(Decision, new Decision());
void Decision::expose(ssq::Table& t) {
    auto x = t.addClass<DecisionContext>("DecisionContext", std::function<DecisionContext*()>([]() { return nullptr; }),
                                         false);
    x.addFunc("set", &DecisionContext::set);
    x.addFunc("get", &DecisionContext::get);
    x.addFunc("addTransition", &DecisionContext::addTransition);
    x.addFunc("setState", &DecisionContext::setState);
    x.addFunc("trigger", &DecisionContext::trigger);
    x.addFunc("state", &DecisionContext::state);
    x.addFunc("utility", [](DecisionContext*, const std::string& s) { return DecisionContext::utility(s); });
    x.addFunc("choose", [](DecisionContext*, const std::string& s) { return DecisionContext::choose(s); });
    x.addFunc("newGrid", &DecisionContext::newGrid);
    x.addFunc("setCell", &DecisionContext::setCell);
    x.addFunc("addCell", &DecisionContext::addCell);
    x.addFunc("sample", &DecisionContext::sample);
    x.addFunc("snapshotJson", &DecisionContext::snapshotJson);
    x.addFunc("restoreJson", &DecisionContext::restoreJson);
    x.addFunc("lastError", &DecisionContext::lastError);
    auto c = t.addClass(name, Decision::create, false);
    expose(c);
}
void Decision::expose(ssq::Class& c) {
    c.addFunc("getName", &Decision::getName);
    c.addFunc("newContext", &Decision::newContext);
}
}  // namespace eve::decision
