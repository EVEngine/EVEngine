#include "common/StateValue.h"

#include <cctype>
#include <utility>

namespace eve {
namespace {

/**
 * @brief Split one path segment ("field[2][3]" or "[0]") into the object key
 *        (empty for pure-index segments) and the array indexes it descends.
 */
bool parseSegment(const std::string& seg, std::string& key, std::vector<size_t>& indexes) {
    key.clear();
    indexes.clear();
    if (seg.empty()) return false;

    size_t i = 0;
    if (seg[0] != '[') {
        while (i < seg.size() && seg[i] != '[') key.push_back(seg[i++]);
    }
    if (key.empty() && (i >= seg.size())) return false;

    while (i < seg.size()) {
        if (seg[i] != '[') return false;
        const size_t close = seg.find(']', i);
        if (close == std::string::npos) return false;
        const std::string idx = seg.substr(i + 1, close - i - 1);
        if (idx.empty() || idx.find_first_not_of("0123456789") != std::string::npos) return false;
        indexes.push_back(static_cast<size_t>(std::stoull(idx)));
        i = close + 1;
    }
    return true;
}

}  // namespace

StateValue StateValue::integer(int64_t v) {
    StateValue sv(Kind::Int);
    sv.i_ = v;
    return sv;
}

StateValue StateValue::number(double v) {
    StateValue sv(Kind::Float);
    sv.f_ = v;
    return sv;
}

StateValue StateValue::boolean(bool v) {
    StateValue sv(Kind::Bool);
    sv.b_ = v;
    return sv;
}

StateValue StateValue::string(std::string v) {
    StateValue sv(Kind::String);
    sv.s_ = std::move(v);
    return sv;
}

void StateValue::pushBack(StateValue v) { arr_.push_back(std::move(v)); }

void StateValue::set(const std::string& key, StateValue v) {
    for (auto& kv : obj_) {
        if (kv.first == key) {
            kv.second = std::move(v);
            return;
        }
    }
    obj_.emplace_back(key, std::move(v));
}

const StateValue* StateValue::find(const std::string& key) const {
    for (const auto& kv : obj_)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

StateValue* StateValue::find(const std::string& key) {
    for (auto& kv : obj_)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

std::vector<std::string> StateValue::keys() const {
    std::vector<std::string> out;
    out.reserve(obj_.size());
    for (const auto& kv : obj_) out.push_back(kv.first);
    return out;
}

const StateValue* StateValue::get(const std::string& dottedPath) const {
    const StateValue* cur   = this;
    size_t            start = 0;
    while (true) {
        const size_t      dot  = dottedPath.find('.', start);
        const bool        last = (dot == std::string::npos);
        const std::string seg  = dottedPath.substr(start, last ? std::string::npos : dot - start);

        std::string         key;
        std::vector<size_t> indexes;
        if (!parseSegment(seg, key, indexes)) return nullptr;
        if (!key.empty()) {
            cur = cur->find(key);
            if (!cur) return nullptr;
        }
        for (const size_t idx : indexes) {
            if (!cur->isArray() || idx >= cur->arr_.size()) return nullptr;
            cur = &cur->arr_[idx];
        }
        if (last) return cur;
        start = dot + 1;
    }
}

StateValue* StateValue::get(const std::string& dottedPath) {
    return const_cast<StateValue*>(static_cast<const StateValue*>(this)->get(dottedPath));
}

bool StateValue::setPath(const std::string& dottedPath, StateValue v) {
    std::vector<std::string> segs;
    size_t                   start = 0;
    while (true) {
        const size_t dot = dottedPath.find('.', start);
        if (dot == std::string::npos) {
            segs.push_back(dottedPath.substr(start));
            break;
        }
        segs.push_back(dottedPath.substr(start, dot - start));
        start = dot + 1;
    }
    if (segs.empty() || (segs.size() == 1 && segs[0].empty())) return false;

    StateValue* cur = this;
    for (size_t si = 0; si + 1 < segs.size(); ++si) {
        std::string         key;
        std::vector<size_t> indexes;
        // Intermediate segments must be plain object keys; array traversal is
        // only supported on the final segment.
        if (!parseSegment(segs[si], key, indexes) || key.empty() || !indexes.empty()) return false;
        if (!cur->isObject()) return false;
        StateValue* next = cur->find(key);
        if (!next) {
            cur->set(key, StateValue::object());
            next = cur->find(key);
        }
        cur = next;
    }

    std::string         key;
    std::vector<size_t> indexes;
    if (!parseSegment(segs.back(), key, indexes)) return false;
    if (!indexes.empty()) {
        if (!key.empty()) {
            if (!cur->isObject()) return false;
            const StateValue* arr = cur->find(key);
            if (!arr || !arr->isArray()) return false;
            cur = cur->find(key);
        }
        if (!cur->isArray() || indexes.size() != 1 || indexes[0] >= cur->arr_.size()) return false;
        cur->arr_[indexes[0]] = std::move(v);
        return true;
    }
    if (!cur->isObject() || key.empty()) return false;
    cur->set(key, std::move(v));
    return true;
}

bool StateValue::mergeDefaults(const StateValue& defaults) {
    if (!isObject() || !defaults.isObject()) return false;
    bool changed = false;
    for (const auto& kv : defaults.obj_) {
        StateValue* mine = find(kv.first);
        if (!mine) {
            set(kv.first, kv.second);
            changed = true;
        } else if (mine->isObject() && kv.second.isObject()) {
            changed = mine->mergeDefaults(kv.second) || changed;
        }
    }
    return changed;
}

bool StateValue::operator==(const StateValue& o) const {
    if (kind_ != o.kind_) return false;
    switch (kind_) {
        case Kind::Null: return true;
        case Kind::Int: return i_ == o.i_;
        case Kind::Float: return f_ == o.f_;
        case Kind::Bool: return b_ == o.b_;
        case Kind::String: return s_ == o.s_;
        case Kind::Array: return arr_ == o.arr_;
        case Kind::Object: return obj_ == o.obj_;
    }
    return false;
}

}  // namespace eve
