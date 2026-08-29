#pragma once

/**
 * @file ResourceRef.h
 * @brief Strong asset, definition, object and generic resource references.
 */

#include "common/Identity.h"
#include "common/Uri.h"

#include <cstdint>
#include <functional>
#include <ostream>
#include <string_view>
#include <utility>
#include <variant>

namespace eve {

/** @brief The alternative held by a generic ResourceRef. */
enum class ResourceRefKind : std::uint8_t {
    Asset,
    Uri,
    Definition,
    Object,
};

/** @brief Returns the stable resource-reference alternative spelling. */
[[nodiscard]] constexpr std::string_view resourceRefKindName(ResourceRefKind kind) noexcept {
    switch (kind) {
        case ResourceRefKind::Asset: return "asset";
        case ResourceRefKind::Uri: return "uri";
        case ResourceRefKind::Definition: return "definition";
        case ResourceRefKind::Object: return "object";
    }
    return "unknown";
}

/** @brief Writes a resource-reference alternative for diagnostics. */
inline std::ostream& operator<<(std::ostream& stream, ResourceRefKind kind) {
    return stream << resourceRefKindName(kind);
}

/**
 * @brief Stable asset identity backed by PersistentId.
 *
 * Its canonical text form is `asset://<uuid>`.  A project-relative path is a
 * ResourceUri locator, not an AssetRef, so renaming a project file does not
 * change this identity.
 */
class EVENGINE_API AssetRef {
public:
    /** @brief Parse an `asset://<canonical UUID>` reference. */
    [[nodiscard]] static Result<AssetRef> parse(std::string_view text);

    /** @brief Build an asset reference from a non-nil persistent identity. */
    [[nodiscard]] static Result<AssetRef> fromId(PersistentId id);

    /** @brief Convert an `asset://<canonical UUID>` ResourceUri to an AssetRef. */
    [[nodiscard]] static Result<AssetRef> fromUri(const ResourceUri& uri);

    /** @brief Return the stable asset identity. */
    [[nodiscard]] const PersistentId& id() const noexcept { return id_; }

    /** @brief Return canonical `asset://<uuid>` text. */
    [[nodiscard]] std::string format() const;

    /** @brief Compare asset references by persistent identity. */
    friend bool operator==(const AssetRef&, const AssetRef&) noexcept = default;

private:
    explicit AssetRef(PersistentId id) : id_(id) {}

    PersistentId id_;
};

/**
 * @brief Stable definition reference backed by LogicalId.
 *
 * A DefinitionRef names a definition (`namespace:name`); it is not a resource
 * locator and therefore is intentionally not accepted as a ResourceUri.
 */
class EVENGINE_API DefinitionRef {
public:
    /** @brief Constructs an invalid reference for optional/component storage. */
    DefinitionRef() = default;

    /** @brief Parse a `namespace:name` logical definition identifier. */
    [[nodiscard]] static Result<DefinitionRef> parse(std::string_view text);

    /** @brief Build a definition reference from a valid LogicalId. */
    [[nodiscard]] static Result<DefinitionRef> fromId(LogicalId id);

    /** @brief Return the stable logical definition identity. */
    [[nodiscard]] const LogicalId& id() const noexcept { return id_; }

    /** @brief Return canonical `namespace:name` text. */
    [[nodiscard]] const std::string& format() const noexcept { return id_.format(); }

    /** @brief Compare definition references by logical identity. */
    friend bool operator==(const DefinitionRef&, const DefinitionRef&) noexcept = default;

private:
    explicit DefinitionRef(LogicalId id) : id_(std::move(id)) {}

    LogicalId id_;
};

/**
 * @brief Stable world/object identity backed by PersistentId.
 *
 * ObjectRef is deliberately distinct from AssetRef even though both use the
 * same underlying UUID representation.  It is not a filesystem URI.
 */
class EVENGINE_API ObjectRef {
public:
    /** @brief Parse a canonical UUID text as an object reference. */
    [[nodiscard]] static Result<ObjectRef> parse(std::string_view text);

    /** @brief Build an object reference from a non-nil persistent identity. */
    [[nodiscard]] static Result<ObjectRef> fromId(PersistentId id);

    /** @brief Return the stable object identity. */
    [[nodiscard]] const PersistentId& id() const noexcept { return id_; }

    /** @brief Return canonical UUID text without a resource URI scheme. */
    [[nodiscard]] std::string format() const { return id_.format(); }

    /** @brief Compare object references by persistent identity. */
    friend bool operator==(const ObjectRef&, const ObjectRef&) noexcept = default;

private:
    explicit ObjectRef(PersistentId id) : id_(id) {}

    PersistentId id_;
};

/**
 * @brief Type-safe union of the reference forms accepted by resource-facing APIs.
 *
 * The variant keeps AssetRef, ResourceUri, DefinitionRef and ObjectRef
 * distinguishable at compile time.  `parse()` parses URI text only; use the
 * named factories for definition and object references, which intentionally
 * have different textual domains.
 */
class EVENGINE_API ResourceRef {
public:
    /** @brief Parse a resource URI, specializing `asset://UUID` as AssetRef. */
    [[nodiscard]] static Result<ResourceRef> parse(std::string_view text);

    /** @brief Wrap an asset reference in the generic reference union. */
    [[nodiscard]] static ResourceRef fromAsset(AssetRef value) { return ResourceRef(std::move(value)); }

    /** @brief Wrap a resource URI in the generic reference union. */
    [[nodiscard]] static ResourceRef fromUri(ResourceUri value) { return ResourceRef(std::move(value)); }

    /** @brief Wrap a definition reference in the generic reference union. */
    [[nodiscard]] static ResourceRef fromDefinition(DefinitionRef value) { return ResourceRef(std::move(value)); }

    /** @brief Wrap an object reference in the generic reference union. */
    [[nodiscard]] static ResourceRef fromObject(ObjectRef value) { return ResourceRef(std::move(value)); }

    /** @brief Return the active reference kind. */
    [[nodiscard]] ResourceRefKind kind() const noexcept;

    /**
     * @brief Return the AssetRef when active, otherwise null.
     * @return Borrowed pointer into this ResourceRef; nullptr when another kind is active.
     * @ownership Borrowed; ResourceRef owns the referenced value.
     * @nullable Yes.
     * @lifetime Valid until this ResourceRef is destroyed or mutated.
     * @thread Affine to the owning ResourceRef; no internal synchronization.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] const AssetRef* asset() const noexcept;

    /**
     * @brief Return the ResourceUri when active, otherwise null.
     * @return Borrowed pointer into this ResourceRef; nullptr when another kind is active.
     * @ownership Borrowed; ResourceRef owns the referenced value.
     * @nullable Yes.
     * @lifetime Valid until this ResourceRef is destroyed or mutated.
     * @thread Affine to the owning ResourceRef; no internal synchronization.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] const ResourceUri* uri() const noexcept;

    /**
     * @brief Return the DefinitionRef when active, otherwise null.
     * @return Borrowed pointer into this ResourceRef; nullptr when another kind is active.
     * @ownership Borrowed; ResourceRef owns the referenced value.
     * @nullable Yes.
     * @lifetime Valid until this ResourceRef is destroyed or mutated.
     * @thread Affine to the owning ResourceRef; no internal synchronization.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] const DefinitionRef* definition() const noexcept;

    /**
     * @brief Return the ObjectRef when active, otherwise null.
     * @return Borrowed pointer into this ResourceRef; nullptr when another kind is active.
     * @ownership Borrowed; ResourceRef owns the referenced value.
     * @nullable Yes.
     * @lifetime Valid until this ResourceRef is destroyed or mutated.
     * @thread Affine to the owning ResourceRef; no internal synchronization.
     * @reentrancy Does not invoke callbacks.
     */
    [[nodiscard]] const ObjectRef* object() const noexcept;

    /** @brief Compare generic references including their active kind. */
    friend bool operator==(const ResourceRef&, const ResourceRef&) noexcept = default;

private:
    explicit ResourceRef(AssetRef value) : value_(std::move(value)) {}
    explicit ResourceRef(ResourceUri value) : value_(std::move(value)) {}
    explicit ResourceRef(DefinitionRef value) : value_(std::move(value)) {}
    explicit ResourceRef(ObjectRef value) : value_(std::move(value)) {}

    std::variant<AssetRef, ResourceUri, DefinitionRef, ObjectRef> value_;
};

}  // namespace eve

namespace std {

template <>
struct hash<eve::AssetRef> {
    /** @brief Hash an AssetRef by its persistent identity. */
    std::size_t operator()(const eve::AssetRef& value) const noexcept {
        return std::hash<eve::PersistentId>{}(value.id());
    }
};

template <>
struct hash<eve::DefinitionRef> {
    /** @brief Hash a DefinitionRef by its logical identity. */
    std::size_t operator()(const eve::DefinitionRef& value) const noexcept {
        return std::hash<eve::LogicalId>{}(value.id());
    }
};

template <>
struct hash<eve::ObjectRef> {
    /** @brief Hash an ObjectRef by its persistent identity. */
    std::size_t operator()(const eve::ObjectRef& value) const noexcept {
        return std::hash<eve::PersistentId>{}(value.id());
    }
};

}  // namespace std
