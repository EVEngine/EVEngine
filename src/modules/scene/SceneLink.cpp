#include "scene/SceneLink.h"

#include <cstring>
#include <string>
#include <vector>

namespace eve::scene {
namespace {

struct Entry {
    std::string name;
    LinkOps ops;
};

/**
 * Function-local so registration from another module's static initialiser
 * cannot run before the table exists. Ids are indices and never reused, so a
 * SceneLink's kind stays valid for the life of the process.
 */
std::vector<Entry>& table() {
    static std::vector<Entry> t;
    return t;
}

}  // namespace

int registerLinkKind(const LinkOps& ops) {
    if (!ops.kind || !ops.pushWorld) return -1;
    const int existing = findLinkKind(ops.kind);
    if (existing >= 0) return existing;
    table().push_back({ops.kind, ops});
    // Point the stored ops at our own copy of the name so a caller may pass a
    // temporary.
    Entry& e = table().back();
    e.ops.kind = e.name.c_str();
    return int(table().size()) - 1;
}

int findLinkKind(const char* kind) {
    if (!kind) return -1;
    const auto& t = table();
    for (size_t i = 0; i < t.size(); ++i)
        if (t[i].name == kind) return int(i);
    return -1;
}

const LinkOps* linkOps(int kindId) {
    const auto& t = table();
    if (kindId < 0 || size_t(kindId) >= t.size()) return nullptr;
    return &t[size_t(kindId)].ops;
}

const char* linkKindName(int kindId) {
    const LinkOps* ops = linkOps(kindId);
    return ops ? ops->kind : "";
}

}  // namespace eve::scene
