#include "common/definitions/DefinitionRuntime.h"

#include <exception>
#include <utility>

namespace eve::definition {
namespace {

/**
 * @brief Owns the candidate payload of one reload or restore.
 *
 * Every early return, including the exception paths of the payload's own copy or
 * assignment, has to release the candidate. Holding it in one guard keeps the
 * failure paths from leaking and keeps the commit a single pointer swap.
 */
class PayloadGuard {
public:
    PayloadGuard(void* payload, RuntimeInstanceCore::DestroyFunction destroy) noexcept
        : payload_(payload), destroy_(destroy) {}
    ~PayloadGuard() { reset(); }

    PayloadGuard(const PayloadGuard&)            = delete;
    PayloadGuard& operator=(const PayloadGuard&) = delete;

    /** @brief Destroy the current payload and take ownership of another one. */
    void adopt(void* payload) noexcept {
        reset();
        payload_ = payload;
    }

    [[nodiscard]] void* get() const noexcept { return payload_; }

    /**
     * @brief Give up ownership and hand the payload to the caller.
     * @return The payload, which the caller must destroy; the guard is left empty.
     */
    [[nodiscard]] void* release() noexcept {
        void* payload = payload_;
        payload_      = nullptr;
        return payload;
    }

    /** @brief Destroy the payload. */
    void reset() noexcept {
        if (payload_ != nullptr && destroy_ != nullptr) destroy_(payload_);
        payload_ = nullptr;
    }

private:
    void*                                payload_ = nullptr;
    RuntimeInstanceCore::DestroyFunction destroy_ = nullptr;
};

}  // namespace

RuntimeInstanceCore::RuntimeInstanceCore(InstanceIdentity identity, void* state, CloneFunction clone,
                                         AssignFunction assign, DestroyFunction destroy, bool active) noexcept
    : identity_(std::move(identity)),
      state_(state),
      clone_(clone),
      assign_(assign),
      destroy_(destroy),
      active_(active) {}

RuntimeInstanceCore::~RuntimeInstanceCore() {
    if (state_ != nullptr && destroy_ != nullptr) destroy_(state_);
}

RuntimeInstanceCore::RuntimeInstanceCore(const RuntimeInstanceCore& other)
    : identity_(other.identity_),
      state_(other.clone_(other.state_)),
      clone_(other.clone_),
      assign_(other.assign_),
      destroy_(other.destroy_),
      active_(other.active_) {}

RuntimeInstanceCore& RuntimeInstanceCore::operator=(const RuntimeInstanceCore& other) {
    if (this == &other) return *this;
    // Copy first: a throwing state copy must leave this instance untouched.
    void* candidate = other.clone_(other.state_);
    if (state_ != nullptr && destroy_ != nullptr) destroy_(state_);
    identity_ = other.identity_;
    state_    = candidate;
    clone_    = other.clone_;
    assign_   = other.assign_;
    destroy_  = other.destroy_;
    active_   = other.active_;
    return *this;
}

RuntimeInstanceCore::RuntimeInstanceCore(RuntimeInstanceCore&& other) noexcept
    : identity_(std::move(other.identity_)),
      state_(other.state_),
      clone_(other.clone_),
      assign_(other.assign_),
      destroy_(other.destroy_),
      active_(other.active_) {
    other.state_ = nullptr;
}

RuntimeInstanceCore& RuntimeInstanceCore::operator=(RuntimeInstanceCore&& other) noexcept {
    if (this == &other) return *this;
    if (state_ != nullptr && destroy_ != nullptr) destroy_(state_);
    identity_    = std::move(other.identity_);
    state_       = other.state_;
    clone_       = other.clone_;
    assign_      = other.assign_;
    destroy_     = other.destroy_;
    active_      = other.active_;
    other.state_ = nullptr;
    return *this;
}

eve::Result<void> RuntimeInstanceCore::checkDefinition(const DefinitionHandle& handle) const {
    if (!handle.isValid())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "definition handle is invalid", "handle", {}, "common.definitions"));
    if (handle != identity_.definitionHandle())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "runtime instance refers to a different definition generation", "handle",
            {}, "common.definitions"));
    return eve::Result<void>::success();
}

eve::Result<ReloadOutcome> RuntimeInstanceCore::reload(const DefinitionHandle& next, ReloadPolicy policy,
                                                       const void* defaults, const RebuildFunction& rebuild) {
    if (!next.isValid())
        return eve::Result<ReloadOutcome>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                          "next definition handle is invalid", "next",
                                                                          {}, "common.definitions"));
    if (next.reference != identity_.definition)
        return eve::Result<ReloadOutcome>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "definition reload targets a different logical definition", "next.reference",
            {}, "common.definitions"));
    if (next.generation < identity_.definitionGeneration)
        return eve::Result<ReloadOutcome>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "definition reload handle is older than the instance generation",
            "next.generation", {}, "common.definitions"));

    ReloadOutcome outcome{
        identity_.instanceId,        identity_.definition, identity_.definitionGeneration, next.generation, policy,
        ReloadDisposition::Unchanged};
    if (next.generation == identity_.definitionGeneration)
        return eve::Result<ReloadOutcome>::success(std::move(outcome), eve::Status::success(eve::StatusCode::NoOp));

    if (policy == ReloadPolicy::RejectWhileActive && active_)
        return eve::Result<ReloadOutcome>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "active runtime instance rejects definition reload",
                                   "policy", {}, "common.definitions"));
    if (policy == ReloadPolicy::ReapplyDefaults && (defaults == nullptr || assign_ == nullptr))
        return eve::Result<ReloadOutcome>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                          "reapply policy requires typed defaults",
                                                                          "defaults", {}, "common.definitions"));

    try {
        PayloadGuard      candidate(clone_(state_), destroy_);
        ReloadDisposition disposition = ReloadDisposition::Kept;
        if (policy == ReloadPolicy::ReapplyDefaults) {
            assign_(candidate.get(), defaults);
            disposition = ReloadDisposition::DefaultsReapplied;
        } else if (policy == ReloadPolicy::RebuildInstance) {
            if (!rebuild)
                return eve::Result<ReloadOutcome>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "rebuild policy requires a rebuild callback", "rebuild", {},
                    "common.definitions"));
            auto rebuilt = rebuild(state_, identity_, next);
            if (!rebuilt) return eve::Result<ReloadOutcome>::failure(rebuilt.status());
            candidate.adopt(std::move(rebuilt).takeValue());
            disposition = ReloadDisposition::Rebuilt;
        } else if (policy == ReloadPolicy::RejectWhileActive) {
            // The active guard was handled above. Inactive instances have
            // the documented keep-values behavior.
            disposition = ReloadDisposition::Kept;
        }

        if (state_ != nullptr && destroy_ != nullptr) destroy_(state_);
        state_                         = candidate.release();
        identity_.definitionGeneration = next.generation;
        outcome.disposition            = disposition;
        return eve::Result<ReloadOutcome>::success(std::move(outcome), eve::Status::success(eve::StatusCode::Applied));
    } catch (const std::exception&) {
        return eve::Result<ReloadOutcome>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "definition reload candidate preparation failed", {},
                                   {}, "common.definitions"));
    } catch (...) {
        return eve::Result<ReloadOutcome>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::Failed, "definition reload candidate preparation failed", {},
                                   {}, "common.definitions"));
    }
}

eve::Result<void> RuntimeInstanceCore::restoreExact(const InstanceIdentity& identity, void* state, bool active) {
    PayloadGuard candidate(state, destroy_);
    if (!identity.isValid())
        return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                 "runtime snapshot identity is invalid", "identity", {},
                                                                 "common.definitions"));
    if (identity.instanceId != identity_.instanceId || identity.definition != identity_.definition)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "runtime snapshot belongs to a different instance or definition", "identity",
            {}, "common.definitions"));
    if (identity.definitionGeneration != identity_.definitionGeneration)
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "runtime snapshot definition generation is not current",
            "identity.definitionGeneration", {}, "common.definitions"));

    if (state_ != nullptr && destroy_ != nullptr) destroy_(state_);
    state_  = candidate.release();
    active_ = active;
    return eve::Result<void>::success();
}

}  // namespace eve::definition
