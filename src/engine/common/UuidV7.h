#pragma once

/**
 * @file UuidV7.h
 * @brief UUIDv7 generation kept separate from lightweight identity values.
 */

#include "common/Identity.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace eve {

/**
 * @brief Entropy callback used by UUIDv7 generation.
 * @param bytes A writable span that the callback must fill completely on success.
 * @return true only when all requested bytes were filled with suitable entropy.
 * @remarks Callers must provide a seed; no implicit pseudo-random source or
 *          `random_device` path is used.
 *          Security-sensitive callers must inject a cryptographically secure source.
 */
using UuidEntropySource = std::function<bool(std::span<std::uint8_t> bytes)>;

/** @brief Clock callback used by the injectable UUIDv7 generator. */
using UuidClock = std::function<std::chrono::system_clock::time_point()>;

/**
 * @brief Injectable UUIDv7 generator.
 *
 * UUIDv7 contains a Unix-millisecond timestamp and random bits. This class
 * assembles the value but does not provide entropy itself. The caller owns
 * the security and availability properties of the injected entropy source.
 */
class EVENGINE_API_FOUNDATION UuidV7Generator {
public:
    /**
     * @brief Creates a generator with an injected entropy source and optional clock.
     * @param entropy Source for the UUID random portion; an empty source makes generation fail.
     * @param clock Source for the timestamp; an empty source uses system_clock at call time.
     */
    explicit UuidV7Generator(UuidEntropySource entropy, UuidClock clock = {});

    /**
     * @brief Generates a UUIDv7 using the injected clock.
     * @return A generated PersistentId, or empty when entropy/time is unavailable or out of range.
     */
    [[nodiscard]] std::optional<PersistentId> generate() const;

    /**
     * @brief Generates a UUIDv7 for an explicit timestamp.
     * @param timestamp Unix time used for the UUIDv7 millisecond field.
     * @return A generated PersistentId, or empty when entropy/time is unavailable or out of range.
     */
    [[nodiscard]] std::optional<PersistentId> generate(std::chrono::system_clock::time_point timestamp) const;

private:
    UuidEntropySource entropy_;
    UuidClock         clock_;
};

/**
 * @brief Constructs a UUIDv7 without owning a clock or generator object.
 * @param timestamp Unix time used for the UUIDv7 millisecond field.
 * @param entropy Source for the UUID random portion.
 * @return A generated PersistentId, or empty when entropy/time is unavailable or out of range.
 */
[[nodiscard]] EVENGINE_API_FOUNDATION std::optional<PersistentId> generateUuidV7(
    std::chrono::system_clock::time_point timestamp, const UuidEntropySource& entropy);

}  // namespace eve
