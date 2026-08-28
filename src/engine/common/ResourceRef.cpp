#include "common/ResourceRef.h"

#include <utility>

namespace eve {
namespace {

template <typename T>
[[nodiscard]] Result<T> parseFailure(std::string message, std::string_view path) {
    return Result<T>::failure(Diagnostic::error(DiagnosticCode::ParseError, std::move(message), std::string(path)));
}

template <typename T>
[[nodiscard]] Result<T> statusFailure(const Status& status) {
    return Result<T>::failure(status);
}

}  // namespace

Result<AssetRef> AssetRef::parse(std::string_view text) {
    auto uri = ResourceUri::parse(text);
    if (!uri.ok()) return statusFailure<AssetRef>(uri.status());
    return fromUri(std::move(uri).takeValue());
}

Result<AssetRef> AssetRef::fromId(PersistentId id) {
    if (id.isNil()) return parseFailure<AssetRef>("asset identity must not be nil", "asset://");
    return Result<AssetRef>::success(AssetRef(id));
}

Result<AssetRef> AssetRef::fromUri(const ResourceUri& uri) {
    if (uri.scheme() != UriScheme::Asset || !uri.query().empty() || !uri.fragment().empty()) {
        return parseFailure<AssetRef>("AssetRef requires an asset:// UUID without query or fragment", uri.format());
    }
    const auto parsed = PersistentId::parse(uri.path());
    if (!parsed || parsed->isNil()) {
        return parseFailure<AssetRef>("asset URI path must be a non-nil canonical UUID", uri.format());
    }
    return Result<AssetRef>::success(AssetRef(*parsed));
}

std::string AssetRef::format() const { return std::string("asset://") + id_.format(); }

Result<DefinitionRef> DefinitionRef::parse(std::string_view text) {
    const auto logical = LogicalId::parse(text);
    if (!logical) return parseFailure<DefinitionRef>("definition reference must be namespace:name", text);
    return Result<DefinitionRef>::success(DefinitionRef(*logical));
}

Result<DefinitionRef> DefinitionRef::fromId(LogicalId id) {
    if (!id.isValid()) return parseFailure<DefinitionRef>("definition identity must be valid", {});
    return Result<DefinitionRef>::success(DefinitionRef(std::move(id)));
}

Result<ObjectRef> ObjectRef::parse(std::string_view text) {
    const auto parsed = PersistentId::parse(text);
    if (!parsed || parsed->isNil()) return parseFailure<ObjectRef>("object reference must be a non-nil UUID", text);
    return Result<ObjectRef>::success(ObjectRef(*parsed));
}

Result<ObjectRef> ObjectRef::fromId(PersistentId id) {
    if (id.isNil()) return parseFailure<ObjectRef>("object identity must not be nil", {});
    return Result<ObjectRef>::success(ObjectRef(id));
}

Result<ResourceRef> ResourceRef::parse(std::string_view text) {
    auto uri = ResourceUri::parse(text);
    if (!uri.ok()) return statusFailure<ResourceRef>(uri.status());
    auto value = std::move(uri).takeValue();
    if (value.scheme() == UriScheme::Asset) {
        auto asset = AssetRef::fromUri(value);
        if (!asset.ok()) return statusFailure<ResourceRef>(asset.status());
        return Result<ResourceRef>::success(ResourceRef(std::move(asset).takeValue()));
    }
    return Result<ResourceRef>::success(ResourceRef(std::move(value)));
}

ResourceRefKind ResourceRef::kind() const noexcept {
    if (std::holds_alternative<AssetRef>(value_)) return ResourceRefKind::Asset;
    if (std::holds_alternative<ResourceUri>(value_)) return ResourceRefKind::Uri;
    if (std::holds_alternative<DefinitionRef>(value_)) return ResourceRefKind::Definition;
    return ResourceRefKind::Object;
}

const AssetRef* ResourceRef::asset() const noexcept { return std::get_if<AssetRef>(&value_); }

const ResourceUri* ResourceRef::uri() const noexcept { return std::get_if<ResourceUri>(&value_); }

const DefinitionRef* ResourceRef::definition() const noexcept { return std::get_if<DefinitionRef>(&value_); }

const ObjectRef* ResourceRef::object() const noexcept { return std::get_if<ObjectRef>(&value_); }

}  // namespace eve
