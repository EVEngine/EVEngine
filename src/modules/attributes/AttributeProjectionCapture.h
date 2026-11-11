#pragma once
/** @file AttributeProjectionCapture.h
 * @brief attributes module-interface: detached native projection facts and prepared exact restoration.
 * @remarks attributes.projection v1 wraps canonical values without ECS links, callbacks or capability registration.
 */
#include <functional>
#include <memory>
#include <optional>
#include "common/Export.h"
#include "common/Result.h"
#include "common/Revision.h"
#include "common/Value.h"
namespace eve::attributes {
class AttributeProjection;
class AttributeSet;
/** @brief Immutable full native values, modifier/counter state, revision and initialization flag.
 * @ownership Owns detached storage, no source entity/Table/policy lifetime retained.
 * @thread Owner-thread use, including derived value queries; accessors invoke no policies or callbacks.
 */
class EVENGINE_API_FOUNDATION AttributeProjectionCapture final {
public:
    /** @brief Release facts without capability publication. */
    ~AttributeProjectionCapture();
    /** @brief Transfer facts. */
    AttributeProjectionCapture(AttributeProjectionCapture&&) noexcept;
    /** @brief Replace facts. */
    AttributeProjectionCapture& operator=(AttributeProjectionCapture&&) noexcept;
    AttributeProjectionCapture(const AttributeProjectionCapture&)            = delete;
    AttributeProjectionCapture& operator=(const AttributeProjectionCapture&) = delete;
    /** @brief Borrow exact canonical values until move/destruction, or empty after move. */
    [[nodiscard]] std::optional<std::reference_wrapper<const AttributeSet>> values() const noexcept;
    /** @brief Source revision, or zero after move. */
    [[nodiscard]] Revision revision() const noexcept;
    /** @brief Source initialization flag, or empty after move. */
    [[nodiscard]] std::optional<bool> initialized() const noexcept;
    /** @brief Encode attributes.projection v1 with exact revision/initialization and canonical set facts.
     * @return Owning value or moved/invalid state diagnostics, without policies or capability registration.
     * @remarks Excludes physical owner handles and derived caches. Custom implementation identity remains
     * domain-owned; this value format does not authorize policy execution or full game restoration.
     * @cost Copies and validates projection-sized facts per explicit persistent encoding.
     */
    [[nodiscard]] Result<Value> encodeState() const;
    /** @brief Admit strict attributes.projection v1 without reseeding, renumbering or advancing revision.
     * @param state Versioned value borrowed only during parsing; result owns accepted facts.
     * @return Independent capture or field/version/counter/initialization diagnostics, without publication.
     * @remarks Unknown fields/versions reject; no implicit migration. Publication still uses the native prepared
     * projection and physical owner/subject/View guards. No custom policy or source service is consulted.
     * @thread Owner thread outside callbacks; no adapter registration.
     * @cost Parses and copies projection-sized facts once per explicit admission.
     */
    [[nodiscard]] static Result<AttributeProjectionCapture> prepareState(const Value& state);


private:
    friend class AttributeProjection;
    friend class PreparedAttributeProjection;
    struct Impl;
    explicit AttributeProjectionCapture(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
/** @brief Independent native candidate; target owner is checked only at publication.
 * @thread Owner-thread preparation and publication outside callbacks; no capability registration.
 */
class EVENGINE_API_FOUNDATION PreparedAttributeProjection final {
public:
    /** @brief Discard unpublished state. */
    ~PreparedAttributeProjection();
    /** @brief Transfer candidate. */
    PreparedAttributeProjection(PreparedAttributeProjection&&) noexcept;
    /** @brief Replace candidate. */
    PreparedAttributeProjection& operator=(PreparedAttributeProjection&&) noexcept;
    PreparedAttributeProjection(const PreparedAttributeProjection&)            = delete;
    PreparedAttributeProjection& operator=(const PreparedAttributeProjection&) = delete;
    /** @brief Clone canonical facts without reseeding or renumbering modifiers.
     * @return Owning candidate or moved capture; source and all targets remain unchanged.
     * @cost Copies native values/modifiers once per restore attempt; no capability registration.
     * @throws std::bad_alloc No state was published.
     */
    [[nodiscard]] static Result<PreparedAttributeProjection> prepare(const AttributeProjectionCapture& captured);

private:
    friend class AttributeProjection;
    struct Impl;
    explicit PreparedAttributeProjection(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
}  // namespace eve::attributes
