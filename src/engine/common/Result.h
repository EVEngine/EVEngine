#pragma once

/**
 * @file Result.h
 * @brief Move-only, checked operation results for the common layer.
 */

#include "common/Assert.h"
#include "common/Status.h"

#include <exception>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace eve {
namespace detail {

/**
 * @brief Debug-only observation token shared by Result<T> specializations.
 *
 * The token starts active for a newly-created Result. A move transfers its
 * responsibility after payload movement succeeds; a moved-from Result is
 * disarmed. The two bytes remain present in non-assert builds so Result keeps
 * one ABI when a Release engine library is consumed by an assertion-enabled
 * test or tool; the observation operations themselves compile to no-ops.
 */
class ResultObservation {
public:
    ResultObservation() noexcept = default;

    ResultObservation(const ResultObservation&)            = delete;
    ResultObservation& operator=(const ResultObservation&) = delete;

    /** @brief Start an inactive token used while a Result move is constructed. */
    struct InactiveTag {};
    explicit ResultObservation(InactiveTag) noexcept {
        mustObserve_ = false;
    }

#if !defined(ZEROERR_NO_ASSERT)
    ~ResultObservation() noexcept(false) {
        const bool observationSatisfied = !mustObserve_ || observed_;
        EV_ASSERT(observationSatisfied, "Result destroyed without checking; inspect it or call ignore() explicitly");
    }
#else
    ~ResultObservation() noexcept = default;
#endif

protected:
    void observe() const noexcept {
#if !defined(ZEROERR_NO_ASSERT)
        observed_ = true;
#endif
    }

    void assertCanBeOverwritten() const {
#if !defined(ZEROERR_NO_ASSERT)
        const bool observationSatisfied = !mustObserve_ || observed_;
        EV_ASSERT(observationSatisfied, "move assignment would overwrite an unchecked Result");
#endif
    }

    void adoptObservationFrom(ResultObservation& other) noexcept {
#if !defined(ZEROERR_NO_ASSERT)
        observed_          = other.observed_;
        mustObserve_       = other.mustObserve_;
        other.mustObserve_ = false;
#else
        (void)other;
#endif
    }

private:
    // Do not condition these fields on ZEROERR_NO_ASSERT. Public Result<T>
    // values cross static/shared-library boundaries whose assertion policy may
    // intentionally differ from the consumer (notably Release unit tests).
    mutable bool observed_    = false;
    bool         mustObserve_ = true;
};

template <class T>
using ResultReturn = std::invoke_result_t<T>;

}  // namespace detail

/**
 * @brief Move-only operation result carrying either a value or Status.
 *
 * Every Result is checked in an assertion-enabled build before destruction.
 * Calling `ok`, `status`, `value`, `error`, a composition helper, `ignore`, or
 * `expect` counts as an explicit observation. In release-style builds where
 * `ZEROERR_NO_ASSERT` is defined, observation checks have no branch cost. The
 * small observation token retains a stable ABI and the class remains
 * `[[nodiscard]]` at compile time.
 *
 * @tparam T Owning value type. References and void use a different form.
 */
template <class T>
class [[nodiscard("Result must be checked or explicitly ignored")]] Result : private detail::ResultObservation {
    static_assert(!std::is_reference_v<T>, "Result<T> cannot hold a reference; use a handle or value");
    static_assert(!std::is_void_v<T>, "Result<void> has a dedicated specialization");

public:
    /** @brief Construct a successful result owning `value`. */
    static Result success(T value) { return Result(Status::success(), std::optional<T>(std::move(value))); }

    /** @brief Construct a successful result with an explicit non-error status. */
    static Result success(T value, Status status) {
        EV_ASSERT(status.isSuccess(), "Result::success requires a successful Status");
        return Result(std::move(status), std::optional<T>(std::move(value)));
    }

    /** @brief Construct a failed result from a structured status. */
    static Result failure(Status status) {
        EV_ASSERT(status.isFailure(), "Result::failure requires a failure Status");
        return Result(std::move(status), std::nullopt);
    }

    /** @brief Construct a failed result from one structured diagnostic. */
    static Result failure(Diagnostic diagnostic) { return failure(Status::failure(std::move(diagnostic))); }

    Result(const Result&)            = delete;
    Result& operator=(const Result&) = delete;

    /** @brief Move a result and transfer its debug observation responsibility. */
    Result(Result&& other)
        : detail::ResultObservation(detail::ResultObservation::InactiveTag{}),
          status_(std::move(other.status_)),
          value_(std::move(other.value_)) {
        adoptObservationFrom(other);
    }

    /**
     * @brief Move-assign a result after checking the destination's old result.
     * @remarks In Debug, overwriting an unobserved destination asserts before
     *          either result's observation responsibility is changed.
     */
    Result& operator=(Result&& other) {
        if (this == &other) return *this;
        assertCanBeOverwritten();
        status_ = std::move(other.status_);
        value_  = std::move(other.value_);
        adoptObservationFrom(other);
        return *this;
    }

    /** @brief Whether this result represents a non-failure outcome. */
    bool ok() const noexcept {
        observe();
        return status_.isSuccess() && value_.has_value();
    }

    /** @brief Whether the result owns a value; observing this is a check. */
    bool hasValue() const noexcept {
        observe();
        return status_.isSuccess() && value_.has_value();
    }

    /** @brief Inspect the structured operation status. */
    const Status& status() const noexcept {
        observe();
        return status_;
    }

    /** @brief Inspect the stable operation code. */
    StatusCode code() const noexcept {
        observe();
        return status_.code();
    }

    /** @brief Inspect all diagnostics; this counts as checking the Result. */
    const std::vector<Diagnostic>& diagnostics() const noexcept {
        observe();
        return status_.diagnostics();
    }

    /**
     * @brief Inspect the first diagnostic, or null when none was supplied.
     * @return Borrowed pointer into this Result's Status; nullptr when no diagnostic exists.
     * @ownership Borrowed; the Result owns the diagnostic storage.
     * @nullable Yes.
     * @lifetime Valid until this Result is destroyed or move-assigned.
     * @thread Affine to this Result; no synchronization.
     * @reentrancy Does not invoke callbacks.
     */
    const Diagnostic* error() const noexcept {
        observe();
        return status_.primaryDiagnostic();
    }

    /** @brief Explicit boolean conversion for control-flow checks. */
    explicit operator bool() const noexcept { return ok(); }

    /** @brief Borrow the value from a const lvalue after checking success. */
    const T& value() const& {
        observe();
        const bool hasSuccessfulValue = status_.isSuccess() && value_.has_value();
        EV_ASSERT(hasSuccessfulValue, "Result::value requires a successful Result with a value");
        return value_.value();
    }

    /** @brief Borrow the value from a mutable lvalue after checking success. */
    T& value() & {
        observe();
        const bool hasSuccessfulValue = status_.isSuccess() && value_.has_value();
        EV_ASSERT(hasSuccessfulValue, "Result::value requires a successful Result with a value");
        return value_.value();
    }

    /** @brief Move the value out after checking success. */
    T&& value() && {
        observe();
        const bool hasSuccessfulValue = status_.isSuccess() && value_.has_value();
        EV_ASSERT(hasSuccessfulValue, "Result::value requires a successful Result with a value");
        return std::move(value_.value());
    }

    /** @brief Move the value out and remove it from this Result. */
    T takeValue() && {
        observe();
        const bool hasSuccessfulValue = status_.isSuccess() && value_.has_value();
        EV_ASSERT(hasSuccessfulValue, "Result::takeValue requires a successful Result with a value");
        T result = std::move(value_.value());
        value_.reset();
        return result;
    }

    /** @brief Return the value or an explicit alternate value, consuming this Result. */
    T valueOr(T fallback) && {
        observe();
        if (status_.isSuccess() && value_.has_value()) return std::move(value_.value());
        return fallback;
    }

    /**
     * @brief Compose a successful value with a function returning another Result.
     * @param function Invoked with the owned value only on success.
     * @return The function's Result, or this Result's failure status.
     */
    template <class Function>
    auto andThen(Function&& function) && -> std::invoke_result_t<Function, T&&> {
        observe();
        using Return = std::invoke_result_t<Function, T&&>;
        if (!status_.isSuccess() || !value_.has_value()) return Return::failure(status_);
        return std::invoke(std::forward<Function>(function), std::move(value_.value()));
    }

    /**
     * @brief Recover a failure with a function receiving its Status.
     * @param function Invoked only on failure and expected to return Result<T>.
     * @return The recovery result or this successful Result.
     */
    template <class Function>
    Result orElse(Function&& function) && {
        observe();
        if (status_.isSuccess() && value_.has_value()) return std::move(*this);
        return std::invoke(std::forward<Function>(function), status_);
    }

    /**
     * @brief Explicitly discard this result after documenting the reason.
     * @param reason Human-readable reason for intentionally ignoring the result.
     */
    void ignore(std::string_view reason = {}) const noexcept {
        (void)reason;
        observe();
    }

    /**
     * @brief Require success and move out the value.
     * @param message Context to include in the assertion on failure.
     * @return The owned successful value.
     * @throws zeroerr::AssertionData in assertion-enabled builds on failure.
     */
    T expect(std::string_view message) && {
        observe();
        if (!status_.isSuccess() || !value_.has_value()) {
            const std::string context(message);
            const std::string detail = status_.describe();
            EV_ASSERT(false, "%s: %s", context.c_str(), detail.c_str());
            std::terminate();
        }
        return std::move(value_.value());
    }

private:
    Result(Status status, std::optional<T> value) : status_(std::move(status)), value_(std::move(value)) {}

    Status           status_;
    std::optional<T> value_;
};

/**
 * @brief Move-only operation result for actions with no value payload.
 */
template <>
class [[nodiscard("Result must be checked or explicitly ignored")]] Result<void> : private detail::ResultObservation {
public:
    /** @brief Construct a successful void result. */
    static Result success() { return Result(Status::success()); }

    /** @brief Construct a successful void result with an explicit status. */
    static Result success(Status status) {
        EV_ASSERT(status.isSuccess(), "Result::success requires a successful Status");
        return Result(std::move(status));
    }

    /** @brief Construct a failed void result from a structured status. */
    static Result failure(Status status) {
        EV_ASSERT(status.isFailure(), "Result::failure requires a failure Status");
        return Result(std::move(status));
    }

    /** @brief Construct a failed void result from one structured diagnostic. */
    static Result failure(Diagnostic diagnostic) { return failure(Status::failure(std::move(diagnostic))); }

    Result(const Result&)            = delete;
    Result& operator=(const Result&) = delete;

    /** @brief Move a result and transfer its debug observation responsibility. */
    Result(Result&& other)
        : detail::ResultObservation(detail::ResultObservation::InactiveTag{}), status_(std::move(other.status_)) {
        adoptObservationFrom(other);
    }

    /** @brief Move-assign after checking the destination's old result. */
    Result& operator=(Result&& other) {
        if (this == &other) return *this;
        assertCanBeOverwritten();
        status_ = std::move(other.status_);
        adoptObservationFrom(other);
        return *this;
    }

    /** @brief Whether this result represents a non-failure outcome. */
    bool ok() const noexcept {
        observe();
        return status_.isSuccess();
    }

    /** @brief Inspect the structured operation status. */
    const Status& status() const noexcept {
        observe();
        return status_;
    }

    /** @brief Inspect the stable operation code. */
    StatusCode code() const noexcept {
        observe();
        return status_.code();
    }

    /** @brief Inspect all diagnostics; this counts as checking the Result. */
    const std::vector<Diagnostic>& diagnostics() const noexcept {
        observe();
        return status_.diagnostics();
    }

    /**
     * @brief Inspect the first diagnostic, or null when none was supplied.
     * @return Borrowed pointer into this Result's Status; nullptr when no diagnostic exists.
     * @ownership Borrowed; the Result owns the diagnostic storage.
     * @nullable Yes.
     * @lifetime Valid until this Result is destroyed or move-assigned.
     * @thread Affine to this Result; no synchronization.
     * @reentrancy Does not invoke callbacks.
     */
    const Diagnostic* error() const noexcept {
        observe();
        return status_.primaryDiagnostic();
    }

    /** @brief Explicit boolean conversion for control-flow checks. */
    explicit operator bool() const noexcept { return ok(); }

    /** @brief Mark a successful void operation as checked. */
    void value() const {
        observe();
        EV_ASSERT(status_.isSuccess(), "Result<void>::value requires a successful Result");
    }

    /**
     * @brief Explicitly discard this result after documenting the reason.
     * @param reason Human-readable reason for intentionally ignoring the result.
     */
    void ignore(std::string_view reason = {}) const noexcept {
        (void)reason;
        observe();
    }

    /**
     * @brief Require success for a void operation.
     * @param message Context to include in the assertion on failure.
     * @throws zeroerr::AssertionData in assertion-enabled builds on failure.
     */
    void expect(std::string_view message) const {
        observe();
        if (!status_.isSuccess()) {
            const std::string context(message);
            const std::string detail = status_.describe();
            EV_ASSERT(false, "%s: %s", context.c_str(), detail.c_str());
            std::terminate();
        }
    }

private:
    explicit Result(Status status) : status_(std::move(status)) {}

    Status status_;
};

}  // namespace eve
