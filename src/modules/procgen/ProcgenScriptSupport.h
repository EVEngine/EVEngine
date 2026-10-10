#pragma once

#include "procgen/Procgen.h"
#include "procgen/ProcgenScriptObjects.h"
#include "procgen/ArtifactPublish.h"
#include "procgen/GeneratedArtifact.h"
#include "procgen/ParamSchema.h"
#include "procgen/heightmap/TerrainFile.h"

#include "common/Result.h"
#include "common/SquirrelBinding.h"
#include "common/SquirrelOwnership.h"

#include "image/ImageData.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <any>
#include <string>
#include <functional>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

namespace eve::procgen {

template <class T>
eve::Result<T> procgenBindingFailure(eve::DiagnosticCode code, std::string message, std::string path = {});

template <class T>
struct NativeProxyReleases {
    struct Record {
        std::any                           reference;
        std::function<eve::Result<void>()> release;
    };
    static inline std::mutex                     mutex;
    static inline std::unordered_map<T*, Record> records;
};

template <class Ref, class T>
std::optional<Ref> nativeProxyReference(T* native);

template <class T>
SQInteger releaseNativeProxy(SQUserPointer pointer, SQInteger);

template <class T, class Ref, class Resolve, class Release>
ssq::Table makeOwnedNativeProxy(HSQUIRRELVM vm, eve::Result<Ref>&& reference, Resolve&& resolve, Release&& release);

template <class T>
ssq::Table projectBorrowedResult(HSQUIRRELVM vm, eve::script::Borrowed<T> borrowed, const char* objectName);

ssq::Table makeOwnedPointSetProxy(HSQUIRRELVM vm, eve::Result<ProcgenPointSetHandleRef>&& reference);
ssq::Table makeOwnedGridProxy(HSQUIRRELVM vm, eve::Result<ProcgenGridHandleRef>&& reference);
ssq::Table makeOwnedSpatialProxy(HSQUIRRELVM vm, eve::Result<ProcgenSpatialDataHandleRef>&& reference);

eve::Value boundsValue(const Bounds& bounds);
eve::Value artifactProjection(GeneratedArtifact&& artifact);
eve::Value publishReceiptProjection(ArtifactPublishReceipt&& receipt);

template <class Ref, class Proxy, class Release>
ssq::Table makeOwnedProxy(HSQUIRRELVM vm, eve::Result<Ref>&& reference, Release&& release);

ssq::Table projectDecodedTerrainResult(HSQUIRRELVM vm, eve::Result<DecodedTerrainFile>&& decoded);
ssq::Table projectRecipeDescriptorResult(HSQUIRRELVM vm, eve::Result<RecipeDescriptor>&& result);

template <class T, class Tag>
eve::Result<eve::script::RuntimeHandleRef<Tag>> ownProcgenObject(eve::script::RuntimeObjectRegistry<T, Tag>& registry,
                                                                 eve::script::Owned<T>                       object);

// ---- template implementations (header-only for TU sharing) ----

template <class T>
eve::Result<T> procgenBindingFailure(eve::DiagnosticCode code, std::string message, std::string path) {
    return eve::Result<T>::failure(
        eve::Diagnostic::error(code, std::move(message), std::move(path), {}, "procgen.squirrel"));
}

template <class Ref, class T>
std::optional<Ref> nativeProxyReference(T* native) {
    if (!native) return std::nullopt;
    std::lock_guard lock(NativeProxyReleases<T>::mutex);
    const auto      found = NativeProxyReleases<T>::records.find(native);
    if (found == NativeProxyReleases<T>::records.end()) return std::nullopt;
    if (const auto* reference = std::any_cast<Ref>(&found->second.reference)) return *reference;
    return std::nullopt;
}

template <class T>
SQInteger releaseNativeProxy(SQUserPointer pointer, SQInteger) {
    auto* native = static_cast<T*>(pointer);
    if (!native) return 0;

    std::function<eve::Result<void>()> release;
    {
        std::lock_guard lock(NativeProxyReleases<T>::mutex);
        const auto      found = NativeProxyReleases<T>::records.find(native);
        if (found == NativeProxyReleases<T>::records.end()) return 0;
        release = std::move(found->second.release);
        NativeProxyReleases<T>::records.erase(found);
    }
    release().ignore("release Squirrel procgen native proxy");
    return 0;
}

template <class T, class Ref, class Resolve, class Release>
ssq::Table makeOwnedNativeProxy(HSQUIRRELVM vm, eve::Result<Ref>&& reference, Resolve&& resolve, Release&& release) {
    if (!reference) return eve::script::projectStatusResult(vm, reference.status());

    const auto ref  = std::move(reference).takeValue();
    const auto view = std::invoke(std::forward<Resolve>(resolve), ref);
    if (!view.isBound()) {
        std::invoke(release, ref).ignore("rollback unbound Squirrel procgen proxy");
        return eve::script::projectStatusResult(
            vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::StaleHandle,
                                                                  "procgen proxy could not resolve its owned object",
                                                                  "procgen", {}, "procgen.squirrel"))
                    .status());
    }

    const SQInteger top      = sq_gettop(vm);
    const size_t    hashCode = eve::script::detail::squirrelTypeHash<T*>();
    sq_pushobject(vm, ssq::detail::getClassObj(vm, hashCode));
    if (SQ_FAILED(sq_createinstance(vm, -1))) {
        sq_settop(vm, top);
        std::invoke(release, ref).ignore("rollback failed Squirrel procgen instance");
        return eve::script::projectStatusResult(
            vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                                                  "failed to create Squirrel procgen proxy", "procgen",
                                                                  {}, "procgen.squirrel"))
                    .status());
    }
    sq_remove(vm, -2);
    auto* native = view.get();
    {
        std::lock_guard lock(NativeProxyReleases<T>::mutex);
        NativeProxyReleases<T>::records.emplace(
            native,
            typename NativeProxyReleases<T>::Record{
                ref, [ref, release = std::forward<Release>(release)]() mutable { return std::invoke(release, ref); }});
    }
    sq_setinstanceup(vm, -1, native);
    sq_settypetag(vm, -1, reinterpret_cast<SQUserPointer>(hashCode));
    sq_setreleasehook(vm, -1, &releaseNativeProxy<T>);

    ssq::Instance value(vm);
    sq_getstackobj(vm, -1, &value.getRaw());
    sq_addref(vm, &value.getRaw());
    sq_settop(vm, top);

    auto result = eve::script::projectStatusResult(vm, eve::Status::success(eve::StatusCode::Applied));
    result.set("value", value);
    result.set("ownership", std::string("owned"));
    result.set("ownerEpoch", static_cast<std::int64_t>(ref.ownerEpoch));
    result.set("handle", static_cast<std::int64_t>(ref.packed()));
    return result;
}

template <class T>
ssq::Table projectBorrowedResult(HSQUIRRELVM vm, eve::script::Borrowed<T> borrowed, const char* objectName) {
    if (!borrowed.isBound())
        return eve::script::projectStatusResult(
            vm, eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::Failed,
                                                                  std::string(objectName) + " could not be produced",
                                                                  objectName, {}, "procgen.squirrel"))
                    .status());
    auto result = eve::script::projectStatusResult(vm, eve::Status::success(eve::StatusCode::Applied));
    result.set("value", borrowed.get());
    result.set("ownership", std::string("borrowed"));
    return result;
}

template <class Ref, class Proxy, class Release>
ssq::Table makeOwnedProxy(HSQUIRRELVM vm, eve::Result<Ref>&& reference, Release&& release) {
    if (!reference) return eve::script::projectStatusResult(vm, reference.status());
    const Ref ref    = std::move(reference).takeValue();
    auto      object = eve::script::makeOwnedSquirrelInstance<Proxy>(vm, std::make_unique<Proxy>(ref));
    if (!object) {
        const eve::Status status = object.status();
        object.ignore("failed to create owned procgen proxy");
        std::invoke(std::forward<Release>(release), ref).ignore("rollback failed owned procgen allocation");
        return eve::script::projectStatusResult(vm, status);
    }
    ssq::Object owned = std::move(object).takeValue();
    auto        result = eve::script::projectStatusResult(vm, eve::Status::success(eve::StatusCode::Applied));
    result.set("value", owned);
    result.set("ownership", std::string("owned"));
    result.set("ownerEpoch", static_cast<std::int64_t>(ref.ownerEpoch));
    result.set("handle", static_cast<std::int64_t>(ref.packed()));
    return result;
}

template <class T, class Tag>
eve::Result<eve::script::RuntimeHandleRef<Tag>> ownProcgenObject(eve::script::RuntimeObjectRegistry<T, Tag>& registry,
                                                                 eve::script::Owned<T>                       object) {
    return registry.emplace(std::move(object));
}

}  // namespace eve::procgen
