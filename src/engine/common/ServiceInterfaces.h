#pragma once

// Small, capability-registered service interfaces for the engine's platform
// services (filesystem / network / timer).
//
// Providers register a live implementation with eve::cap::provide<I>(impl) at
// module construction; consumers query with eve::cap::query<I>() and cope with
// nullptr. Tests provide deterministic mock implementations so error paths can
// be exercised offline (see test/service_mocks.cpp).

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace eve::service {

/**
 * @brief Minimal file read / write / existence surface.
 * Providers: eve::filesystem::Filesystem. Mocks can simulate missing files,
 * read failures, and full disks without touching PhysFS.
 */
class IFileSystem {
public:
    static constexpr const char *capabilityName = "IFileSystem";
    virtual ~IFileSystem() = default;

    /** @brief Reads an entire file. @return false when missing or unreadable. */
    virtual bool readFile(const std::string &path, std::vector<uint8_t> &out) = 0;
    /** @brief Writes bytes to a file. @return false when the write fails. */
    virtual bool writeFile(const std::string &path, const void *data, size_t size) = 0;
    /** @brief True when the path exists (file or directory). */
    virtual bool fileExists(const std::string &path) = 0;
};

/**
 * @brief Minimal synchronous HTTP request surface.
 * Providers: eve::network::Network. Mocks return canned status codes / errors,
 * so consumers can test 404 / timeout / TLS paths without a network.
 */
class INetwork {
public:
    static constexpr const char *capabilityName = "INetwork";
    virtual ~INetwork() = default;

    /**
     * @brief Performs one HTTP request synchronously.
     * @param method "GET" / "POST" / ...
     * @param url Full URL.
     * @param body Request body (empty for GET).
     * @param timeoutMs Timeout in milliseconds.
     * @param[out] status HTTP status code (only meaningful when returning true).
     * @param[out] responseBody Response body bytes.
     * @return false on transport error / timeout; true when a response arrived.
     */
    virtual bool httpRequest(const std::string &method, const std::string &url,
                             const std::string &body, int timeoutMs, int &status,
                             std::string &responseBody) = 0;
};

/**
 * @brief Monotonic elapsed-seconds clock.
 * Providers: eve::timer::Timer. Mocks return scripted values so time-dependent
 * logic can be tested deterministically.
 */
class ITimer {
public:
    static constexpr const char *capabilityName = "ITimer";
    virtual ~ITimer() = default;

    /** @brief Seconds since the provider was created (monotonic, non-decreasing). */
    virtual double elapsedSeconds() = 0;
};

}  // namespace eve::service
