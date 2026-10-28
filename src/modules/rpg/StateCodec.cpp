#include "rpg/StateCodec.h"
#include "common/Json.h"
#include "rpg/RPGActor.h"
#include "rpg/Tracker.h"

#include <iomanip>
#include <sstream>

namespace eve::rpg {
namespace {
std::string quote(const std::string& text) {
    std::ostringstream out;
    out << '"';
    for (char c : text) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default: out << c;
        }
    }
    out << '"';
    return out.str();
}
template <class F>
void commaList(std::ostringstream& out, size_t count, F write) {
    for (size_t i = 0; i < count; ++i) {
        if (i) out << ',';
        write(i);
    }
}
}  // namespace

std::string StateCodec::serializeActor(RPGActor* actor) {
    if (!actor) return "null";
    std::ostringstream out;
    out << std::setprecision(17) << "{\"version\":1,\"attributes\":{";
    auto   attributes = actor->attributes();
    size_t n          = 0;
    for (const auto& [name, value] : attributes->values) {
        if (n++) out << ',';
        out << quote(name) << ':' << value.base;
    }
    out << "},\"progression\":{";
    auto p = actor->progression();
    out << "\"level\":" << p->level << ",\"maxLevel\":" << p->maxLevel << ",\"experience\":" << p->experience
        << ",\"baseXp\":" << p->baseXp << ",\"growth\":" << p->growth << ",\"pointsPerLevel\":" << p->pointsPerLevel
        << ",\"unspentPoints\":" << p->unspentPoints << "},\"skills\":[";
    auto skills = actor->skills();
    n           = 0;
    for (const auto& [id, runtime] : skills->known) {
        if (n++) out << ',';
        out << "{\"id\":" << quote(id) << ",\"cooldown\":" << runtime.cooldownRemaining << '}';
    }
    out << "],\"statuses\":[";
    auto statuses = actor->statuses();
    commaList(out, statuses->active.size(), [&](size_t i) {
        const auto& s = statuses->active[i];
        out << "{\"effectId\":" << quote(s.effectId) << ",\"source\":" << quote(s.source) << ",\"stacks\":" << s.stacks
            << ",\"remaining\":" << s.remaining << ",\"periodAccum\":" << s.periodAccum << ",\"props\":{";
        size_t propIndex = 0;
        for (const auto& [key, value] : s.props) {
            if (propIndex++) out << ',';
            out << quote(key) << ':' << quote(value);
        }
        out << "}}";
    });
    out << "]}";
    return out.str();
}

bool StateCodec::deserializeActor(RPGActor* actor, const std::string& json, std::string* error) {
    if (!actor) {
        if (error) *error = "null actor";
        return false;
    }
    std::string e;
    auto        doc = eve::json::Document::parse(json, &e);
    if (!doc.valid() || !doc.root().isObject()) {
        if (error) *error = e.empty() ? "invalid actor snapshot" : e;
        return false;
    }
    auto root = doc.root();
    actor->attributes()->values.clear();
    actor->statuses()->active.clear();
    actor->skills()->known.clear();
    actor->skills()->casting = {};
    auto attrs               = root.get("attributes");
    for (const auto& key : attrs.keys()) actor->setBaseAttribute(key, attrs.get(key.c_str()).asDouble());
    auto p = root.get("progression");
    actor->configureProgression(p.getInt("level", 1), p.getInt("maxLevel", 100), p.getFloat("baseXp", 100.f),
                                p.getFloat("growth", 1.15f), p.getInt("pointsPerLevel", 1));
    actor->progression()->experience    = p.getDouble("experience", 0.0);
    actor->progression()->unspentPoints = p.getInt("unspentPoints", 0);
    auto skills                         = root.get("skills");
    for (size_t i = 0; i < skills.size(); ++i) {
        auto s  = skills.at(i);
        auto id = s.getString("id");
        if (id.empty()) continue;
        actor->learnSkill(id);
        actor->setSkillCooldown(id, s.getFloat("cooldown", 0.f));
    }
    auto statuses = root.get("statuses");
    for (size_t i = 0; i < statuses.size(); ++i) {
        auto s        = statuses.at(i);
        auto id       = s.getString("effectId");
        int  stacks   = std::max(1, s.getInt("stacks", 1));
        int  instance = 0;
        for (int k = 0; k < stacks; ++k) instance = actor->applyEffect(id, s.getString("source"));
        if (instance > 0) {
            for (auto& active : actor->statuses()->active)
                if (active.instanceId == instance) {
                    active.remaining   = s.getFloat("remaining", active.remaining);
                    active.periodAccum = s.getFloat("periodAccum", 0.f);
                    active.props       = s.getStringMap("props");
                }
        }
    }
    return true;
}

std::string StateCodec::serializeTracker(const Tracker* tracker) {
    if (!tracker) return "null";
    std::ostringstream out;
    out << "{\"version\":1,\"entries\":[";
    commaList(out, tracker->order_.size(), [&](size_t i) {
        const auto& r = tracker->entries_.at(tracker->order_[i]);
        out << "{\"id\":" << quote(r.id) << ",\"state\":" << quote(r.state) << ",\"objectives\":[";
        commaList(out, r.objectives.size(), [&](size_t j) {
            const auto& o = r.objectives[j];
            out << "{\"id\":" << quote(o.id) << ",\"current\":" << o.current << ",\"count\":" << o.count
                << ",\"done\":" << (o.done ? "true" : "false") << '}';
        });
        out << "]}";
    });
    out << "]}";
    return out.str();
}

bool StateCodec::deserializeTracker(Tracker* tracker, const std::string& json, std::string* error) {
    if (!tracker) {
        if (error) *error = "null tracker";
        return false;
    }
    std::string e;
    auto        doc = eve::json::Document::parse(json, &e);
    if (!doc.valid() || !doc.root().isObject()) {
        if (error) *error = e.empty() ? "invalid tracker snapshot" : e;
        return false;
    }
    tracker->entries_.clear();
    tracker->order_.clear();
    tracker->pending_.clear();
    tracker->polled_.clear();
    auto entries = doc.root().get("entries");
    for (size_t i = 0; i < entries.size(); ++i) {
        auto         value = entries.at(i);
        QuestRuntime r;
        r.id            = value.getString("id");
        r.state         = value.getString("state");
        auto objectives = value.get("objectives");
        for (size_t j = 0; j < objectives.size(); ++j) {
            auto o = objectives.at(j);
            r.objectives.push_back({o.getString("id"), o.getInt("current"), o.getInt("count", 1), o.getBool("done")});
        }
        if (!r.id.empty()) {
            tracker->order_.push_back(r.id);
            tracker->entries_[r.id] = std::move(r);
        }
    }
    return true;
}

}  // namespace eve::rpg
