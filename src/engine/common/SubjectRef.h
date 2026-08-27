#pragma once

/**
 * @file SubjectRef.h
 * @brief Stable reference to a runtime gameplay subject.
 *
 * SubjectRef identifies a domain object across module boundaries.  It is a
 * persistent identity, not an ECS entity handle and not a definition/content
 * id.  The owning domain is responsible for resolving it and for defining
 * stale/restore behavior.
 */

#include "common/Identity.h"

#include <cstddef>

namespace eve {

/**
 * @brief Strong, domain-neutral reference to a runtime subject.
 *
 * A nil reference is invalid.  There is deliberately no implicit conversion
 * to PersistentId so call sites cannot accidentally mix subject, asset and
 * definition identities.
 */
class SubjectRef {
public:
    /** @brief Construct an invalid nil reference. */
    SubjectRef() = default;

    /** @brief Wrap a persistent identity without changing its bytes. */
    [[nodiscard]] static SubjectRef fromPersistentId(PersistentId id) noexcept { return SubjectRef(id); }

    /** @brief Return the invalid nil reference. */
    [[nodiscard]] static SubjectRef nil() noexcept { return {}; }

    /** @brief Return whether this reference is valid and non-nil. */
    [[nodiscard]] bool isValid() const noexcept { return !id_.isNil(); }

    /** @brief Return the wrapped persistent identity. */
    [[nodiscard]] PersistentId persistentId() const noexcept { return id_; }

    /** @brief Return the canonical lower-case UUID spelling. */
    [[nodiscard]] std::string format() const { return id_.format(); }

    friend bool operator==(const SubjectRef&, const SubjectRef&) noexcept = default;

private:
    explicit SubjectRef(PersistentId id) noexcept : id_(id) {}

    PersistentId id_;
};

}  // namespace eve

namespace std {

template <>
struct hash<eve::SubjectRef> {
    /** @brief Hash a subject reference using its stable identity. */
    size_t operator()(const eve::SubjectRef& value) const noexcept {
        return static_cast<size_t>(value.persistentId().hash());
    }
};

}  // namespace std
