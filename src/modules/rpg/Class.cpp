#include "rpg/Class.h"

#include "common/Json.h"

#include <algorithm>

namespace eve::rpg {

using eve::json::Value;

bool ClassDefinition::hasTag(const std::string &tag) const {
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

std::string ClassDefinition::getExtra(const std::string &key, const std::string &fallback) const {
    auto it = extra.find(key);
    return it == extra.end() ? fallback : it->second;
}

namespace {
std::unordered_map<std::string, ClassDefinition> &classTable() {
    static std::unordered_map<std::string, ClassDefinition> t;
    return t;
}
}  // namespace

void ClassRegistry::registerClass(const ClassDefinition &def) {
    if (def.id.empty()) return;
    classTable()[def.id] = def;
}

const ClassDefinition *ClassRegistry::find(const std::string &id) {
    auto &t = classTable();
    auto it = t.find(id);
    return it == t.end() ? nullptr : &it->second;
}

bool ClassRegistry::remove(const std::string &id) { return classTable().erase(id) > 0; }

void ClassRegistry::clear() { classTable().clear(); }

int ClassRegistry::count() { return int(classTable().size()); }

int ClassRegistry::loadFromJson(const std::string &json, std::string *error) {
    std::string err;
    const eve::json::Document doc = eve::json::Document::parse(json, &err);
    if (!doc.valid()) {
        if (error) *error = err.empty() ? "invalid json" : err;
        return 0;
    }
    const Value root = doc.root();
    int n = 0;
    const auto parseOne = [&](Value o) {
        if (!o.isObject()) return;
        ClassDefinition def;
        def.id = o.getString("id");
        if (def.id.empty()) return;
        def.displayName = o.getString("displayName");
        def.tags = o.getStringArray("tags");
        def.extra = o.getStringMap("extra");
        def.traits = o.getStringArray("traits");
        const Value learn = o.get("learnSkills");
        for (size_t i = 0; i < learn.size(); ++i) {
            const Value e = learn.at(i);
            ClassLearnSkill ls;
            ls.skillId = e.getString("skillId");
            ls.level = e.getInt("level", 1);
            if (ls.level <= 0) ls.level = 1;
            if (ls.skillId.empty()) continue;
            def.learnSkills.push_back(std::move(ls));
        }
        classTable()[def.id] = std::move(def);
        ++n;
    };
    if (root.isArray()) {
        for (size_t i = 0; i < root.size(); ++i) parseOne(root.at(i));
    } else if (root.isObject()) {
        parseOne(root);
    }
    return n;
}

}  // namespace eve::rpg