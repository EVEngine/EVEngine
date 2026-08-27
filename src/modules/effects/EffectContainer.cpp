#include "effects/EffectContainer.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace eve::effects {
namespace {

eve::Result<std::string> rejected(std::string message) {
    return eve::Result<std::string>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, std::move(message)));
}

eve::Result<std::string> conflict(std::string message) {
    return eve::Result<std::string>::failure(
        eve::Status::failure(eve::Diagnostic::error(eve::DiagnosticCode::Conflict, std::move(message))));
}

eve::Result<void> notFound(std::string message) {
    return eve::Result<void>::failure(
        eve::Status::failure(eve::Diagnostic::error(eve::DiagnosticCode::NotFound, std::move(message))));
}

eve::Result<EffectUpdateSummary> invalidDelta() {
    return eve::Result<EffectUpdateSummary>::failure(eve::Diagnostic::error(
        eve::DiagnosticCode::InvalidArgument, "effect update delta must be finite and non-negative"));
}

double normalisedRemaining(double duration) { return duration > 0.0 ? duration : -1.0; }

eve::Result<double> mergeDuration(const EffectInstance& current, const EffectDefinition& definition) {
    switch (definition.policy.duration) {
        case DurationPolicy::Keep: return eve::Result<double>::success(current.remaining);
        case DurationPolicy::Replace: return eve::Result<double>::success(normalisedRemaining(definition.duration));
        case DurationPolicy::Extend:
            if (definition.duration <= 0.0 || current.remaining < 0.0)
                return eve::Result<double>::success(current.remaining);
            if (current.remaining > std::numeric_limits<double>::max() - definition.duration)
                return eve::Result<double>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                           "effect duration extension overflows"));
            return eve::Result<double>::success(current.remaining + definition.duration);
    }
    return eve::Result<double>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "unknown effect duration policy"));
}

eve::Result<double> mergeMagnitude(const EffectInstance& current, const EffectDefinition& definition) {
    switch (definition.policy.magnitude) {
        case MagnitudePolicy::Keep: return eve::Result<double>::success(current.magnitude);
        case MagnitudePolicy::Replace: return eve::Result<double>::success(definition.magnitude);
        case MagnitudePolicy::Add: {
            const double value = current.magnitude + definition.magnitude;
            if (!std::isfinite(value))
                return eve::Result<double>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                           "effect magnitude addition overflows"));
            return eve::Result<double>::success(value);
        }
        case MagnitudePolicy::Max:
            return eve::Result<double>::success(std::max(current.magnitude, definition.magnitude));
    }
    return eve::Result<double>::failure(
        eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "unknown effect magnitude policy"));
}

bool stackWouldOverflow(const EffectInstance& current, const EffectDefinition& definition) {
    if (definition.policy.maxStacks == 0) return false;
    switch (definition.policy.stackCount) {
        case StackCountPolicy::Keep: return current.stackCount > definition.policy.maxStacks;
        case StackCountPolicy::Increment:
            return definition.stackCount > definition.policy.maxStacks - current.stackCount;
        case StackCountPolicy::Set: return definition.stackCount > definition.policy.maxStacks;
    }
    return true;
}

eve::Result<std::uint32_t> mergeStackCount(const EffectInstance& current, const EffectDefinition& definition) {
    std::uint32_t value = current.stackCount;
    switch (definition.policy.stackCount) {
        case StackCountPolicy::Keep: break;
        case StackCountPolicy::Increment:
            if (definition.stackCount > std::numeric_limits<std::uint32_t>::max() - current.stackCount)
                return eve::Result<std::uint32_t>::failure(
                    eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument, "effect stack count overflows"));
            value = current.stackCount + definition.stackCount;
            break;
        case StackCountPolicy::Set: value = definition.stackCount; break;
    }
    if (definition.policy.maxStacks != 0 && value > definition.policy.maxStacks) {
        if (definition.policy.overflow == OverflowPolicy::Clamp)
            value = definition.policy.maxStacks;
        else
            return eve::Result<std::uint32_t>::failure(
                eve::Diagnostic::error(eve::DiagnosticCode::Conflict, "effect stack count exceeds its maximum"));
    }
    return eve::Result<std::uint32_t>::success(value);
}

}  // namespace

EffectContainer::EffectContainer(eve::UuidEntropySource entropy, eve::UuidClock clock) {
    if (entropy) effectIdGenerator_.emplace(std::move(entropy), std::move(clock));
}

EffectContainer::EffectContainer(const EffectContainer& other)
    : nextId_(other.nextId_), nextSequence_(other.nextSequence_),
      effectIdGenerator_(other.effectIdGenerator_), generation_(other.generation_),
      lastTick_(other.lastTick_), hasLastTick_(other.hasLastTick_), events_(other.events_) {
    for (const auto& effect : other.effects_)
        effects_.push_back(effect ? std::make_unique<EffectInstance>(*effect) : nullptr);
}

EffectContainer& EffectContainer::operator=(const EffectContainer& other) {
    if (this == &other) return *this;

    EffectContainer replacement(other);
    using std::swap;
    swap(effects_, replacement.effects_);
    swap(events_, replacement.events_);
    swap(nextId_, replacement.nextId_);
    swap(nextSequence_, replacement.nextSequence_);
    swap(generation_, replacement.generation_);
    swap(lastTick_, replacement.lastTick_);
    swap(hasLastTick_, replacement.hasLastTick_);
    swap(effectIdGenerator_, replacement.effectIdGenerator_);
    return *this;
}

EffectContainer EffectContainer::clone() const { return EffectContainer(*this); }

EffectContainer EffectContainer::snapshot() const { return clone(); }

eve::Result<void> EffectContainer::restore(const EffectContainer& snapshotValue) {
    if (this == &snapshotValue) {
        if (generation_ == std::numeric_limits<std::uint64_t>::max())
            return eve::Result<void>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::InvariantViolation, "effect container generation overflow"));
        ++generation_;
        return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
    }
    if (generation_ == std::numeric_limits<std::uint64_t>::max())
        return eve::Result<void>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "effect container generation overflow"));

    EffectContainer candidate(snapshotValue);
    candidate.generation_ = generation_ + 1;
    *this = std::move(candidate);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

std::string EffectContainer::effectiveKey(const EffectDefinition& definition) const {
    return definition.stackKey.empty() ? definition.id : definition.stackKey;
}

void EffectContainer::emit(EffectEventKind kind, const EffectInstance& effect, const std::string& reason) {
    EffectEvent event;
    event.sequence       = nextSequence_++;
    event.kind           = kind;
    event.effectId       = effect.id;
    event.subject        = effect.subject;
    event.type           = effect.type;
    event.source         = effect.source;
    event.reason         = reason;
    event.effectIdentity = effect.identity;
    event.tick           = lastTick_;
    events_.push_back(std::move(event));
}

EffectContainer::Store::iterator EffectContainer::findIterator(const std::string& id) {
    return std::find_if(effects_.begin(), effects_.end(), [&id](const auto& effect) { return effect->id == id; });
}

EffectContainer::Store::const_iterator EffectContainer::findIterator(const std::string& id) const {
    return std::find_if(effects_.begin(), effects_.end(), [&id](const auto& effect) { return effect->id == id; });
}

EffectContainer::Store::iterator EffectContainer::findIterator(eve::EffectId id) {
    return std::find_if(effects_.begin(), effects_.end(), [id](const auto& effect) {
        return !id.isNil() && effect->identity == id;
    });
}

EffectContainer::Store::const_iterator EffectContainer::findIterator(eve::EffectId id) const {
    return std::find_if(effects_.begin(), effects_.end(), [id](const auto& effect) {
        return !id.isNil() && effect->identity == id;
    });
}

eve::Result<std::string> EffectContainer::apply(const EffectDefinition& definition, const std::string& subject,
                                                const std::string& source) {
    auto validation = definition.validate();
    if (!validation) return eve::Result<std::string>::failure(validation.status());
    if (subject.empty()) return rejected("effect subject must not be empty");

    // Resolve the new instance's initial stack before any Replace/overflow
    // mutation.  A rejected application must leave both instances and events
    // unchanged.
    std::uint32_t initialStacks = definition.stackCount;
    if (definition.policy.maxStacks != 0 && initialStacks > definition.policy.maxStacks) {
        if (definition.policy.overflow == OverflowPolicy::Reject)
            return conflict("effect stack count exceeds its maximum");
        initialStacks = definition.policy.maxStacks;
    }

    const std::string key     = effectiveKey(definition);
    auto              matches = [&subject, &key](const auto& effect) {
        return effect->subject == subject && effect->stackKey == key;
    };
    auto existing = std::find_if(effects_.begin(), effects_.end(), matches);

    if (existing != effects_.end() && definition.policy.stackMode == StackMode::Reuse) {
        EffectInstance& current  = **existing;
        auto            duration = mergeDuration(current, definition);
        if (!duration) return eve::Result<std::string>::failure(duration.status());
        auto magnitude = mergeMagnitude(current, definition);
        if (!magnitude) return eve::Result<std::string>::failure(magnitude.status());
        if (stackWouldOverflow(current, definition) && definition.policy.overflow == OverflowPolicy::ReplaceOldest) {
            emit(EffectEventKind::Removed, current, "overflow");
            effects_.erase(existing);
            existing = effects_.end();
        } else {
            auto stacks = mergeStackCount(current, definition);
            if (!stacks) return eve::Result<std::string>::failure(stacks.status());
            current.priority   = definition.priority;
            current.duration   = definition.duration;
            current.period     = definition.period;
            current.periodElapsed = 0.0;
            current.remaining  = std::move(duration).takeValue();
            current.magnitude  = std::move(magnitude).takeValue();
            current.stackCount = std::move(stacks).takeValue();
            emit(EffectEventKind::Refreshed, current);
            return eve::Result<std::string>::success(current.id, eve::Status::success(eve::StatusCode::Applied));
        }
    }

    if (existing != effects_.end() && definition.policy.stackMode == StackMode::Accumulate) {
        EffectInstance& current  = **existing;
        auto            duration = mergeDuration(current, definition);
        if (!duration) return eve::Result<std::string>::failure(duration.status());
        auto magnitude = mergeMagnitude(current, definition);
        if (!magnitude) return eve::Result<std::string>::failure(magnitude.status());
        if (stackWouldOverflow(current, definition) && definition.policy.overflow == OverflowPolicy::ReplaceOldest) {
            emit(EffectEventKind::Removed, current, "overflow");
            effects_.erase(existing);
            existing = effects_.end();
        } else {
            auto stacks = mergeStackCount(current, definition);
            if (!stacks) {
                if (stacks.code() == eve::StatusCode::Conflict)
                    return conflict("effect stack count exceeds its maximum");
                return eve::Result<std::string>::failure(stacks.status());
            }
            current.priority   = definition.priority;
            current.duration   = definition.duration;
            current.period     = definition.period;
            current.periodElapsed = 0.0;
            current.remaining  = std::move(duration).takeValue();
            current.magnitude  = std::move(magnitude).takeValue();
            current.stackCount = std::move(stacks).takeValue();
            emit(EffectEventKind::Stacked, current);
            return eve::Result<std::string>::success(current.id, eve::Status::success(eve::StatusCode::Applied));
        }
    }

    if (definition.policy.stackMode == StackMode::Replace) {
        for (auto it = effects_.begin(); it != effects_.end();) {
            if (matches(*it)) {
                emit(EffectEventKind::Removed, **it, "replaced");
                it = effects_.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto effect = std::make_unique<EffectInstance>();
    if (effectIdGenerator_) {
        const auto generated = effectIdGenerator_->generate();
        if (!generated)
            return eve::Result<std::string>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Failed, "canonical effect identity generation failed"));
        effect->identity = eve::EffectId::fromUuid(*generated);
        if (find(effect->identity) != nullptr)
            return eve::Result<std::string>::failure(eve::Diagnostic::error(
                eve::DiagnosticCode::Conflict, "canonical effect identity already exists"));
        effect->id       = effect->identity.format();
    } else {
        std::ostringstream id;
        id << "effect-" << std::setw(16) << std::setfill('0') << nextId_++;
        effect->id = id.str();
    }
    effect->subject    = subject;
    effect->type       = definition.id;
    effect->source     = source;
    effect->stackKey   = key;
    effect->priority   = definition.priority;
    effect->duration   = std::max(0.0, definition.duration);
    effect->remaining  = normalisedRemaining(definition.duration);
    effect->period     = definition.period;
    effect->periodElapsed = 0.0;
    effect->magnitude  = definition.magnitude;
    effect->stackCount = initialStacks;
    effect->payload    = definition.payload;
    effect->tags       = definition.tags;
    std::sort(effect->tags.begin(), effect->tags.end());
    effect->tags.erase(std::unique(effect->tags.begin(), effect->tags.end()), effect->tags.end());

    const std::string result = effect->id;
    effects_.push_back(std::move(effect));
    emit(EffectEventKind::Applied, *effects_.back());
    return eve::Result<std::string>::success(result, eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<eve::EffectId> EffectContainer::applyCanonical(const EffectDefinition& definition,
                                                           const std::string& subject,
                                                           const std::string& source) {
    if (!effectIdGenerator_)
        return eve::Result<eve::EffectId>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Unsupported,
            "canonical effect identity requires an injected UUID entropy source"));
    auto result = apply(definition, subject, source);
    if (!result) return eve::Result<eve::EffectId>::failure(result.status());
    const auto id = eve::EffectId::parse(std::move(result).takeValue());
    if (!id || id->isNil())
        return eve::Result<eve::EffectId>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Failed, "effect application did not produce a canonical identity"));
    return eve::Result<eve::EffectId>::success(*id, eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<EffectHandle> EffectContainer::handleFor(const std::string& id) const {
    if (id.empty())
        return eve::Result<EffectHandle>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "effect handle id must not be empty", "id"));
    if (find(id) == nullptr)
        return eve::Result<EffectHandle>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "effect instance is not active", "id"));
    return eve::Result<EffectHandle>::success(
        EffectHandle{id, generation_}, eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<const EffectInstance*> EffectContainer::resolve(EffectHandle handle) const {
    if (!handle.isValid())
        return eve::Result<const EffectInstance*>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "effect handle is invalid", "handle"));
    if (handle.containerGeneration != generation_)
        return eve::Result<const EffectInstance*>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::StaleHandle, "effect handle belongs to an older container generation", "handle"));
    const auto* instance = find(handle.instanceId);
    if (instance == nullptr)
        return eve::Result<const EffectInstance*>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::NotFound, "effect instance is not active", "handle.instanceId"));
    return eve::Result<const EffectInstance*>::success(instance,
                                                       eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<std::string> EffectContainer::apply(const std::string& subject, const std::string& type,
                                                const std::string& source, int priority, double duration,
                                                const std::string& stackKey, StackPolicy policy) {
    EffectDefinition definition;
    definition.id       = type;
    definition.stackKey = stackKey;
    definition.priority = priority;
    definition.duration = duration;
    switch (policy) {
        case StackPolicy::Replace: definition.policy.stackMode = StackMode::Replace; break;
        case StackPolicy::Stack: definition.policy.stackMode = StackMode::NewInstance; break;
        case StackPolicy::Refresh: definition.policy.stackMode = StackMode::Reuse; break;
    }
    auto result = apply(definition, subject, source);
    return result;
}

eve::Result<void> EffectContainer::remove(const std::string& id, const std::string& reason) {
    const auto it = findIterator(id);
    if (it == effects_.end()) return notFound("effect instance is not active");
    emit(EffectEventKind::Removed, **it, reason);
    effects_.erase(it);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<void> EffectContainer::remove(eve::EffectId id, const std::string& reason) {
    const auto it = findIterator(id);
    if (it == effects_.end()) return notFound("effect instance is not active");
    emit(EffectEventKind::Removed, **it, reason);
    effects_.erase(it);
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<EffectUpdateSummary> EffectContainer::advanceInstances(double dtSeconds, eve::SimulationTick tick) {
    if (!std::isfinite(dtSeconds) || dtSeconds < 0.0) return invalidDelta();
    EffectUpdateSummary summary;
    summary.elapsedSeconds = dtSeconds;
    if (dtSeconds == 0.0)
        return eve::Result<EffectUpdateSummary>::success(summary, eve::Status::success(eve::StatusCode::NoOp));

    for (auto it = effects_.begin(); it != effects_.end();) {
        EffectInstance& effect = **it;
        const double activeDelta = effect.remaining < 0.0 ? dtSeconds : std::min(dtSeconds, effect.remaining);
        if (effect.period > 0.0 && activeDelta > 0.0) {
            if (!std::isfinite(effect.periodElapsed) ||
                effect.periodElapsed > std::numeric_limits<double>::max() - activeDelta)
                return eve::Result<EffectUpdateSummary>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument, "effect periodic accumulator overflow"));
            effect.periodElapsed += activeDelta;
            while (effect.periodElapsed >= effect.period) {
                effect.periodElapsed -= effect.period;
                EffectPeriodicTick periodic;
                periodic.effectId       = effect.id;
                periodic.subject        = effect.subject;
                periodic.source         = effect.source;
                periodic.magnitude      = effect.magnitude;
                periodic.stackCount     = effect.stackCount;
                periodic.tags           = effect.tags;
                periodic.effectIdentity = effect.identity;
                periodic.tick            = tick;
                summary.periodicTicks.push_back(std::move(periodic));
                emit(EffectEventKind::Periodic, effect, "periodic");
            }
        }
        if (effect.remaining < 0.0) {
            ++it;
            continue;
        }
        effect.remaining = std::max(0.0, effect.remaining - dtSeconds);
        if (effect.remaining == 0.0) {
            emit(EffectEventKind::Expired, effect);
            ++summary.expired;
            it = effects_.erase(it);
        } else {
            ++it;
        }
    }
    return eve::Result<EffectUpdateSummary>::success(summary, eve::Status::success(eve::StatusCode::Applied));
}

eve::Result<EffectUpdateSummary> EffectContainer::update(double dtSeconds) {
    auto duration = eve::Duration::fromSeconds(dtSeconds);
    if (!duration) return eve::Result<EffectUpdateSummary>::failure(duration.status());

    auto nextTick = hasLastTick_ ? lastTick_.incremented()
                                 : std::optional<eve::SimulationTick>(eve::SimulationTick(1));
    if (!nextTick)
        return eve::Result<EffectUpdateSummary>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvariantViolation, "effect simulation tick overflow"));

    return advance({*nextTick, std::move(duration).takeValue()});
}

eve::Result<EffectUpdateSummary> EffectContainer::advance(const eve::SimulationStep& step) {
    if (step.delta.nanoseconds() < 0)
        return eve::Result<EffectUpdateSummary>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "effect simulation delta must be non-negative"));
    if (hasLastTick_ && step.tick <= lastTick_)
        return eve::Result<EffectUpdateSummary>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::Conflict, "effect simulation tick must advance monotonically"));

    // Stage the lifecycle mutation so a failure does not publish partial state.
    auto candidate = clone();
    candidate.lastTick_ = step.tick;
    candidate.hasLastTick_ = true;
    auto result = candidate.advanceInstances(step.delta.seconds(), step.tick);
    if (!result) return eve::Result<EffectUpdateSummary>::failure(result.status());
    auto status = result.status();
    EffectUpdateSummary summary = std::move(result).takeValue();
    summary.tick = step.tick;
    candidate.lastTick_ = step.tick;
    candidate.hasLastTick_ = true;
    *this = std::move(candidate);
    return eve::Result<EffectUpdateSummary>::success(std::move(summary), std::move(status));
}

void EffectContainer::clear() {
    effects_.clear();
    events_.clear();
    nextId_ = nextSequence_ = 1;
    lastTick_ = eve::SimulationTick::zero();
    hasLastTick_ = false;
    if (generation_ != std::numeric_limits<std::uint64_t>::max()) ++generation_;
}

EffectInstance* EffectContainer::find(const std::string& id) {
    const auto it = findIterator(id);
    return it == effects_.end() ? nullptr : it->get();
}

const EffectInstance* EffectContainer::find(const std::string& id) const {
    const auto it = findIterator(id);
    return it == effects_.end() ? nullptr : it->get();
}

EffectInstance* EffectContainer::find(eve::EffectId id) {
    const auto it = findIterator(id);
    return it == effects_.end() ? nullptr : it->get();
}

const EffectInstance* EffectContainer::find(eve::EffectId id) const {
    const auto it = findIterator(id);
    return it == effects_.end() ? nullptr : it->get();
}

int EffectContainer::effectCount() const { return static_cast<int>(effects_.size()); }

EffectInstance* EffectContainer::effectAt(int index) {
    return index < 0 || static_cast<std::size_t>(index) >= effects_.size()
               ? nullptr
               : effects_[static_cast<std::size_t>(index)].get();
}

const EffectInstance* EffectContainer::effectAt(int index) const {
    return index < 0 || static_cast<std::size_t>(index) >= effects_.size()
               ? nullptr
               : effects_[static_cast<std::size_t>(index)].get();
}

int EffectContainer::subjectCount(const std::string& subject) const {
    return static_cast<int>(std::count_if(effects_.begin(), effects_.end(),
                                          [&subject](const auto& effect) { return effect->subject == subject; }));
}

EffectInstance* EffectContainer::subjectAt(const std::string& subject, int index) {
    if (index < 0) return nullptr;
    for (auto& effect : effects_)
        if (effect->subject == subject && index-- == 0) return effect.get();
    return nullptr;
}

const EffectInstance* EffectContainer::subjectAt(const std::string& subject, int index) const {
    if (index < 0) return nullptr;
    for (const auto& effect : effects_)
        if (effect->subject == subject && index-- == 0) return effect.get();
    return nullptr;
}

int EffectContainer::taggedCount(const std::string& subject, const std::string& tag) const {
    return static_cast<int>(std::count_if(effects_.begin(), effects_.end(), [&subject, &tag](const auto& effect) {
        return effect->subject == subject && effect->hasTag(tag);
    }));
}

EffectInstance* EffectContainer::taggedAt(const std::string& subject, const std::string& tag, int index) {
    if (index < 0) return nullptr;
    for (auto& effect : effects_)
        if (effect->subject == subject && effect->hasTag(tag) && index-- == 0) return effect.get();
    return nullptr;
}

const EffectInstance* EffectContainer::taggedAt(const std::string& subject, const std::string& tag, int index) const {
    if (index < 0) return nullptr;
    for (const auto& effect : effects_)
        if (effect->subject == subject && effect->hasTag(tag) && index-- == 0) return effect.get();
    return nullptr;
}

int EffectContainer::eventCount() const { return static_cast<int>(events_.size()); }

EffectEvent* EffectContainer::eventAt(int index) {
    return index < 0 || static_cast<std::size_t>(index) >= events_.size() ? nullptr
                                                                          : &events_[static_cast<std::size_t>(index)];
}

const EffectEvent* EffectContainer::eventAt(int index) const {
    return index < 0 || static_cast<std::size_t>(index) >= events_.size() ? nullptr
                                                                          : &events_[static_cast<std::size_t>(index)];
}

void EffectContainer::clearEvents() { events_.clear(); }

eve::Result<EffectUpdateSummary> EffectExecutor::advance(EffectContainer& container, double dtSeconds) const {
    return container.update(dtSeconds);
}

eve::Result<EffectUpdateSummary> EffectExecutor::advance(EffectContainer& container,
                                                          const eve::SimulationStep& step) const {
    return container.advance(step);
}

}  // namespace eve::effects
