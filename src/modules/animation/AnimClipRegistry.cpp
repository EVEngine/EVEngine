#include "animation/AnimClipRegistry.h"

#include "animation/AnimClip.h"
#include "animation/AnimImporter.h"
#include "animation/AnimSkeleton.h"

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

eve::Observer<AnimClipRegistry::ReloadEvent>& reloadObservers() {
    static eve::Observer<AnimClipRegistry::ReloadEvent> observer;
    return observer;
}

std::uint64_t& reloadCallbackFailures() {
    static std::uint64_t failures = 0;
    return failures;
}

void notifyReload(AnimClipRegistry::ReloadEvent event) {
    static_cast<void>(reloadObservers().notifyChecked(
        []() noexcept { ++reloadCallbackFailures(); }, event));
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

    eve::Result<bool> reload(const std::string& normPath) override {
        auto result = AnimClipRegistry::reloadPath(normPath);
        if (!result) return eve::Result<bool>::failure(result.status());
        return eve::Result<bool>::success(std::move(result).takeValue() > 0);
    }
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

eve::Result<int> AnimClipRegistry::reloadPath(const std::string& path) {
    const std::string norm = normalizePath(path);
    const auto        it   = entries().find(norm);
    if (it == entries().end() || it->second.empty()) {
        notifyReload({norm, 0, false});
        return eve::Result<int>::success(0, eve::Status::success(eve::StatusCode::NoOp));
    }

    AnimSkeleton* skeleton = nullptr;
    AnimClip*     fresh    = nullptr;
    try {
        AnimImporter::importEvaFile(norm, &skeleton, &fresh);
    } catch (...) {
        delete skeleton;
        delete fresh;
        notifyReload({norm, 0, false});
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "animation clip reload import failed", norm));
    }
    if (!fresh) {
        delete skeleton;
        notifyReload({norm, 0, false});
        return eve::Result<int>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "animation clip reload produced no clip", norm));
    }
    for (AnimClip* clip : it->second) clip->adopt(*fresh);
    const int refreshed = static_cast<int>(it->second.size());
    delete fresh;
    delete skeleton;
    notifyReload({norm, refreshed, true});
    return eve::Result<int>::success(refreshed, eve::Status::success(eve::StatusCode::Applied));
}

eve::Subscription AnimClipRegistry::subscribeReload(ReloadCallback callback) {
    return reloadObservers().subscribe(std::move(callback));
}

std::uint64_t AnimClipRegistry::reloadCallbackFailureCount() { return reloadCallbackFailures(); }

int AnimClipRegistry::count() {
    int n = 0;
    for (const auto& kv : entries()) n += static_cast<int>(kv.second.size());
    return n;
}

void AnimClipRegistry::clear() { entries().clear(); }

}  // namespace eve::animation
