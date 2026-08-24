#include "effects/Effects.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iomanip>
#include <limits>
#include <sstream>

namespace eve::effects {
namespace {

std::string escapeJson(const std::string& value) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    out << '"';
    return out.str();
}

bool plausibleJsonValue(const std::string& json) {
    const auto first = json.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return false;
    const auto last = json.find_last_not_of(" \t\r\n");
    const char head = json[first];
    const char tail = json[last];
    if ((head == '{' && tail == '}') || (head == '[' && tail == ']') || (head == '"' && tail == '"')) return true;
    const std::string value = json.substr(first, last - first + 1);
    if (value == "true" || value == "false" || value == "null") return true;
    char* end = nullptr;
    std::strtod(json.c_str() + first, &end);
    return end == json.c_str() + last + 1;
}

}  // namespace

void EffectPayload::setString(const std::string& key, const std::string& value) {
    if (!key.empty()) values_[key] = escapeJson(value);
}

void EffectPayload::setNumber(const std::string& key, double value) {
    if (key.empty() || !std::isfinite(value)) return;
    std::ostringstream out;
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    values_[key] = out.str();
}

void EffectPayload::setBool(const std::string& key, bool value) {
    if (!key.empty()) values_[key] = value ? "true" : "false";
}

void EffectPayload::setNull(const std::string& key) {
    if (!key.empty()) values_[key] = "null";
}

bool EffectPayload::setJson(const std::string& key, const std::string& json) {
    if (key.empty() || !plausibleJsonValue(json)) return false;
    const auto first = json.find_first_not_of(" \t\r\n");
    const auto last  = json.find_last_not_of(" \t\r\n");
    values_[key]     = json.substr(first, last - first + 1);
    return true;
}

bool        EffectPayload::has(const std::string& key) const { return values_.contains(key); }
bool        EffectPayload::erase(const std::string& key) { return values_.erase(key) != 0; }
std::string EffectPayload::getJson(const std::string& key) const {
    const auto it = values_.find(key);
    return it == values_.end() ? std::string{} : it->second;
}
std::string EffectPayload::toJson() const {
    std::ostringstream out;
    out << '{';
    bool first = true;
    for (const auto& [key, value] : values_) {
        if (!first) out << ',';
        first = false;
        out << escapeJson(key) << ':' << value;
    }
    out << '}';
    return out.str();
}
void EffectPayload::clear() { values_.clear(); }

bool Effect::addTag(const std::string& tag) {
    if (tag.empty()) return false;
    const auto it = std::lower_bound(tags.begin(), tags.end(), tag);
    if (it != tags.end() && *it == tag) return false;
    tags.insert(it, tag);
    return true;
}
bool Effect::removeTag(const std::string& tag) {
    const auto it = std::lower_bound(tags.begin(), tags.end(), tag);
    if (it == tags.end() || *it != tag) return false;
    tags.erase(it);
    return true;
}
bool        Effect::hasTag(const std::string& tag) const { return std::binary_search(tags.begin(), tags.end(), tag); }
int         Effect::tagCount() const { return static_cast<int>(tags.size()); }
std::string Effect::tagAt(int index) const {
    return index < 0 || static_cast<size_t>(index) >= tags.size() ? std::string{} : tags[static_cast<size_t>(index)];
}

std::string policyName(StackPolicy policy) {
    switch (policy) {
        case StackPolicy::Replace: return "replace";
        case StackPolicy::Stack: return "stack";
        case StackPolicy::Refresh: return "refresh";
    }
    return "unknown";
}
bool parsePolicy(const std::string& name, StackPolicy& policy) {
    if (name == "replace")
        policy = StackPolicy::Replace;
    else if (name == "stack")
        policy = StackPolicy::Stack;
    else if (name == "refresh")
        policy = StackPolicy::Refresh;
    else
        return false;
    return true;
}
std::string eventKindName(EffectEventKind kind) {
    switch (kind) {
        case EffectEventKind::Applied: return "applied";
        case EffectEventKind::Refreshed: return "refreshed";
        case EffectEventKind::Expired: return "expired";
        case EffectEventKind::Removed: return "removed";
    }
    return "unknown";
}

std::string EffectContainer::effectiveKey(const std::string& type, const std::string& stackKey) const {
    return stackKey.empty() ? type : stackKey;
}

void EffectContainer::emit(EffectEventKind kind, const Effect& effect, const std::string& reason) {
    events_.push_back({nextSequence_++, kind, effect.id, effect.subject, effect.type, effect.source, reason});
}

EffectContainer::Store::iterator EffectContainer::findIterator(const std::string& id) {
    return std::find_if(effects_.begin(), effects_.end(), [&id](const auto& effect) { return effect->id == id; });
}

std::string EffectContainer::apply(const std::string& subject, const std::string& type, const std::string& source,
                                   int priority, double duration, const std::string& stackKey, StackPolicy policy) {
    if (subject.empty() || type.empty() || !std::isfinite(duration)) return {};
    const std::string key     = effectiveKey(type, stackKey);
    auto              matches = [&subject, &key](const auto& effect) {
        return effect->subject == subject && effect->stackKey == key;
    };
    if (policy == StackPolicy::Refresh) {
        const auto it = std::find_if(effects_.begin(), effects_.end(), matches);
        if (it != effects_.end()) {
            (*it)->priority  = priority;
            (*it)->duration  = std::max(0.0, duration);
            (*it)->remaining = duration > 0.0 ? duration : -1.0;
            emit(EffectEventKind::Refreshed, **it);
            return (*it)->id;
        }
    } else if (policy == StackPolicy::Replace) {
        for (auto it = effects_.begin(); it != effects_.end();) {
            if (matches(*it)) {
                emit(EffectEventKind::Removed, **it, "replaced");
                it = effects_.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto               effect = std::make_unique<Effect>();
    std::ostringstream id;
    id << "effect-" << std::setw(16) << std::setfill('0') << nextId_++;
    effect->id               = id.str();
    effect->subject          = subject;
    effect->type             = type;
    effect->source           = source;
    effect->stackKey         = key;
    effect->priority         = priority;
    effect->duration         = std::max(0.0, duration);
    effect->remaining        = duration > 0.0 ? duration : -1.0;
    const std::string result = effect->id;
    effects_.push_back(std::move(effect));
    emit(EffectEventKind::Applied, *effects_.back());
    return result;
}

bool EffectContainer::remove(const std::string& id, const std::string& reason) {
    const auto it = findIterator(id);
    if (it == effects_.end()) return false;
    emit(EffectEventKind::Removed, **it, reason);
    effects_.erase(it);
    return true;
}

void EffectContainer::update(double dtSeconds) {
    if (dtSeconds <= 0.0 || !std::isfinite(dtSeconds)) return;
    for (auto it = effects_.begin(); it != effects_.end();) {
        Effect& effect = **it;
        if (effect.remaining < 0.0) {
            ++it;
            continue;
        }
        effect.remaining = std::max(0.0, effect.remaining - dtSeconds);
        if (effect.remaining == 0.0) {
            emit(EffectEventKind::Expired, effect);
            it = effects_.erase(it);
        } else {
            ++it;
        }
    }
}

void EffectContainer::clear() {
    effects_.clear();
    events_.clear();
    nextId_ = nextSequence_ = 1;
}
Effect* EffectContainer::find(const std::string& id) {
    const auto it = findIterator(id);
    return it == effects_.end() ? nullptr : it->get();
}
int     EffectContainer::effectCount() const { return static_cast<int>(effects_.size()); }
Effect* EffectContainer::effectAt(int index) {
    return index < 0 || static_cast<size_t>(index) >= effects_.size() ? nullptr
                                                                      : effects_[static_cast<size_t>(index)].get();
}
int EffectContainer::subjectCount(const std::string& subject) const {
    return static_cast<int>(std::count_if(effects_.begin(), effects_.end(),
                                          [&subject](const auto& effect) { return effect->subject == subject; }));
}
Effect* EffectContainer::subjectAt(const std::string& subject, int index) {
    if (index < 0) return nullptr;
    for (auto& effect : effects_) {
        if (effect->subject == subject && index-- == 0) return effect.get();
    }
    return nullptr;
}
int EffectContainer::taggedCount(const std::string& subject, const std::string& tag) const {
    return static_cast<int>(std::count_if(effects_.begin(), effects_.end(), [&subject, &tag](const auto& effect) {
        return effect->subject == subject && effect->hasTag(tag);
    }));
}
Effect* EffectContainer::taggedAt(const std::string& subject, const std::string& tag, int index) {
    if (index < 0) return nullptr;
    for (auto& effect : effects_) {
        if (effect->subject == subject && effect->hasTag(tag) && index-- == 0) return effect.get();
    }
    return nullptr;
}
int          EffectContainer::eventCount() const { return static_cast<int>(events_.size()); }
EffectEvent* EffectContainer::eventAt(int index) {
    return index < 0 || static_cast<size_t>(index) >= events_.size() ? nullptr : &events_[static_cast<size_t>(index)];
}
void EffectContainer::clearEvents() { events_.clear(); }

EffectContainer* Effects::newContainer() {
    Effects* module = Effects::create();
    module->containers_.push_back(std::make_unique<EffectContainer>());
    return module->containers_.back().get();
}

Module_IMPL(Effects, new Effects());

void Effects::expose(ssq::Table& table) {
    auto payload = table.addClass<EffectPayload>(
        "EffectPayload", std::function<EffectPayload*()>([]() -> EffectPayload* { return nullptr; }), false);
    payload.addFunc("setString", &EffectPayload::setString);
    payload.addFunc("setNumber", [](EffectPayload* value, const std::string& key, float number) {
        if (value) value->setNumber(key, number);
    });
    payload.addFunc("setBool", &EffectPayload::setBool);
    payload.addFunc("setNull", &EffectPayload::setNull);
    payload.addFunc("setJson", &EffectPayload::setJson);
    payload.addFunc("has", &EffectPayload::has);
    payload.addFunc("erase", &EffectPayload::erase);
    payload.addFunc("getJson", &EffectPayload::getJson);
    payload.addFunc("toJson", &EffectPayload::toJson);
    payload.addFunc("clear", &EffectPayload::clear);

    auto effect =
        table.addClass<Effect>("Effect", std::function<Effect*()>([]() -> Effect* { return nullptr; }), false);
    effect.addFunc("getId", [](Effect* value) { return value ? value->id : std::string{}; });
    effect.addFunc("getSubject", [](Effect* value) { return value ? value->subject : std::string{}; });
    effect.addFunc("getType", [](Effect* value) { return value ? value->type : std::string{}; });
    effect.addFunc("getSource", [](Effect* value) { return value ? value->source : std::string{}; });
    effect.addFunc("getStackKey", [](Effect* value) { return value ? value->stackKey : std::string{}; });
    effect.addFunc("getPriority", [](Effect* value) { return value ? value->priority : 0; });
    effect.addFunc("getDuration", [](Effect* value) { return value ? static_cast<float>(value->duration) : 0.0f; });
    effect.addFunc("getRemaining", [](Effect* value) { return value ? static_cast<float>(value->remaining) : 0.0f; });
    effect.addFunc("getPayload", [](Effect* value) -> EffectPayload* { return value ? &value->payload : nullptr; });
    effect.addFunc("addTag", &Effect::addTag);
    effect.addFunc("removeTag", &Effect::removeTag);
    effect.addFunc("hasTag", &Effect::hasTag);
    effect.addFunc("tagCount", &Effect::tagCount);
    effect.addFunc("tagAt", &Effect::tagAt);

    auto event = table.addClass<EffectEvent>(
        "EffectEvent", std::function<EffectEvent*()>([]() -> EffectEvent* { return nullptr; }), false);
    event.addFunc("getSequence",
                  [](EffectEvent* value) { return value ? static_cast<int64_t>(value->sequence) : int64_t{0}; });
    event.addFunc("getKind", [](EffectEvent* value) { return value ? eventKindName(value->kind) : std::string{}; });
    event.addFunc("getEffectId", [](EffectEvent* value) { return value ? value->effectId : std::string{}; });
    event.addFunc("getSubject", [](EffectEvent* value) { return value ? value->subject : std::string{}; });
    event.addFunc("getType", [](EffectEvent* value) { return value ? value->type : std::string{}; });
    event.addFunc("getSource", [](EffectEvent* value) { return value ? value->source : std::string{}; });
    event.addFunc("getReason", [](EffectEvent* value) { return value ? value->reason : std::string{}; });

    auto container = table.addClass<EffectContainer>(
        "EffectContainer", std::function<EffectContainer*()>([]() -> EffectContainer* { return nullptr; }), false);
    container.addFunc("apply", [](EffectContainer* value, const std::string& subject, const std::string& type,
                                  const std::string& source, int priority, float duration, const std::string& stackKey,
                                  const std::string& policyNameValue) {
        StackPolicy policy;
        return value && parsePolicy(policyNameValue, policy)
                   ? value->apply(subject, type, source, priority, duration, stackKey, policy)
                   : std::string{};
    });
    container.addFunc("remove", &EffectContainer::remove);
    container.addFunc("update", &EffectContainer::update);
    container.addFunc("clear", &EffectContainer::clear);
    container.addFunc("find", &EffectContainer::find);
    container.addFunc("effectCount", &EffectContainer::effectCount);
    container.addFunc("effectAt", &EffectContainer::effectAt);
    container.addFunc("subjectCount", &EffectContainer::subjectCount);
    container.addFunc("subjectAt", &EffectContainer::subjectAt);
    container.addFunc("taggedCount", &EffectContainer::taggedCount);
    container.addFunc("taggedAt", &EffectContainer::taggedAt);
    container.addFunc("eventCount", &EffectContainer::eventCount);
    container.addFunc("eventAt", &EffectContainer::eventAt);
    container.addFunc("clearEvents", &EffectContainer::clearEvents);

    auto cls = table.addClass(name, Effects::create, false);
    expose(cls);
}

void Effects::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Effects::getName);
    cls.addFunc("newContainer", [](Effects*) { return Effects::newContainer(); });
}

}  // namespace eve::effects
