#include "animation/AnimClipRegistry.h"

#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"

#include "common/AssetReloader.h"
#include "common/Capability.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <utility>

namespace eve::animation {
namespace {

using EntryMap = std::map<std::string, std::vector<AnimClip*>>;

EntryMap& entries() {
    static EntryMap map;
    return map;
}

bool isEvaPath(const std::string& path) {
    if (path.size() < 5) return false;
    std::string ext = path.substr(path.size() - 4);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".eva";
}

/** @brief IAssetReloader: `.eva` changes refresh registered AnimClip instances. */
class AnimClipReloader : public eve::caps::IAssetReloader {
public:
    const char* reloadKind() const override { return "animclip"; }

    bool handlesPath(const std::string& normPath) const override {
        return isEvaPath(AnimClipRegistry::normalizePath(normPath));
    }

    bool reload(const std::string& normPath) override { return AnimClipRegistry::reloadPath(normPath) > 0; }
};

struct Register {
    Register() {
        static AnimClipReloader reloader;
        eve::cap::addListener<eve::caps::IAssetReloader>(&reloader, eve::caps::IAssetReloader::kConsumer);
    }
} g_register;

}  // namespace

std::string AnimClipRegistry::normalizePath(const std::string& path) {
    std::string out = path;
    for (char& c : out)
        if (c == '\\') c = '/';
    return out;
}

void AnimClipRegistry::registerPath(const std::string& path, AnimClip* clip) {
    if (!clip) return;
    std::vector<AnimClip*>& list = entries()[normalizePath(path)];
    if (std::find(list.begin(), list.end(), clip) == list.end()) list.push_back(clip);
}

void AnimClipRegistry::unregister(AnimClip* clip) {
    if (!clip) return;
    for (auto it = entries().begin(); it != entries().end();) {
        std::vector<AnimClip*>& list = it->second;
        list.erase(std::remove(list.begin(), list.end(), clip), list.end());
        if (list.empty())
            it = entries().erase(it);
        else
            ++it;
    }
}

std::vector<AnimClip*> AnimClipRegistry::findByPath(const std::string& path) {
    const auto it = entries().find(normalizePath(path));
    return it == entries().end() ? std::vector<AnimClip*>() : it->second;
}

bool AnimClipRegistry::hasPath(const std::string& path) { return entries().count(normalizePath(path)) > 0; }

int AnimClipRegistry::reloadPath(const std::string& path) {
    const std::string norm = normalizePath(path);
    const auto        it   = entries().find(norm);
    if (it == entries().end() || it->second.empty()) return 0;

    AnimSkeleton* skeleton = nullptr;
    AnimClip*     fresh    = nullptr;
    try {
        AnimImporter::importEvaFile(norm, &skeleton, &fresh);
    } catch (...) {
        delete skeleton;
        delete fresh;
        return 0;
    }
    if (!fresh) {
        delete skeleton;
        return 0;
    }
    for (AnimClip* clip : it->second) clip->adopt(*fresh);
    delete fresh;
    delete skeleton;
    return static_cast<int>(it->second.size());
}

int AnimClipRegistry::count() {
    int n = 0;
    for (const auto& kv : entries()) n += static_cast<int>(kv.second.size());
    return n;
}

void AnimClipRegistry::clear() { entries().clear(); }

}  // namespace eve::animation
