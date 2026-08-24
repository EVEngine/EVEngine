#pragma once

#include "common/Module.h"

#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace eve::effects {

/** @brief Policy used when an effect shares a subject and stack key. */
enum class StackPolicy { Replace, Stack, Refresh };

/** @brief Lifecycle event kind emitted by an effect container. */
enum class EffectEventKind { Applied, Refreshed, Expired, Removed };

/** @brief Deterministic JSON-compatible payload carried by an effect. */
class EffectPayload {
public:
    /** @brief Sets a JSON string field. */
    void setString(const std::string& key, const std::string& value);
    /** @brief Sets a JSON number field. */
    void setNumber(const std::string& key, double value);
    /** @brief Sets a JSON boolean field. */
    void setBool(const std::string& key, bool value);
    /** @brief Sets a JSON null field. */
    void setNull(const std::string& key);
    /** @brief Sets a pre-encoded JSON scalar, array, or object field. */
    bool setJson(const std::string& key, const std::string& json);
    /** @brief Returns whether a field exists. */
    bool has(const std::string& key) const;
    /** @brief Removes a field and returns whether it existed. */
    bool erase(const std::string& key);
    /** @brief Returns a field's JSON fragment, or an empty string. */
    std::string getJson(const std::string& key) const;
    /** @brief Serializes all fields as a deterministic JSON object. */
    std::string toJson() const;
    /** @brief Removes every field. */
    void clear();

private:
    std::map<std::string, std::string> values_;
};

/** @brief One active, subject-agnostic effect fact. */
struct Effect {
    std::string              id;
    std::string              subject;
    std::string              type;
    std::string              source;
    std::string              stackKey;
    int                      priority  = 0;
    double                   duration  = 0.0;
    double                   remaining = -1.0;
    EffectPayload            payload;
    std::vector<std::string> tags;

    /** @brief Adds a tag while retaining deterministic lexical order. */
    bool addTag(const std::string& tag);
    /** @brief Removes a tag and returns whether it existed. */
    bool removeTag(const std::string& tag);
    /** @brief Returns whether this effect has a tag. */
    bool hasTag(const std::string& tag) const;
    /** @brief Returns the number of tags. */
    int tagCount() const;
    /** @brief Returns a tag by lexical index, or an empty string. */
    std::string tagAt(int index) const;
};

/** @brief Deterministically sequenced effect lifecycle event. */
struct EffectEvent {
    uint64_t        sequence = 0;
    EffectEventKind kind     = EffectEventKind::Applied;
    std::string     effectId;
    std::string     subject;
    std::string     type;
    std::string     source;
    std::string     reason;
};

/** @brief Generic container that stores and advances effect facts. */
class EffectContainer {
public:
    /**
     * @brief Applies an effect using the requested collision policy.
     * @param subject Opaque subject identifier.
     * @param type Stable effect type identifier.
     * @param source Stable opaque source identifier.
     * @param priority Ordering metadata retained with the effect.
     * @param duration Seconds until expiry; non-positive means permanent.
     * @param stackKey Collision key; an empty key uses type.
     * @param policy Replace, stack, or refresh behavior.
     * @return Stable effect ID, or an empty string for invalid input.
     */
    std::string apply(const std::string& subject, const std::string& type, const std::string& source, int priority,
                      double duration, const std::string& stackKey, StackPolicy policy);
    /** @brief Removes an active effect and emits a removed event. */
    bool remove(const std::string& id, const std::string& reason = "removed");
    /** @brief Advances finite effects and expires those reaching zero. */
    void update(double dtSeconds);
    /** @brief Removes all effects and events and resets stable counters. */
    void clear();

    /** @brief Returns an active effect by stable ID, or nullptr. */
    Effect* find(const std::string& id);
    /** @brief Returns active effect count in deterministic creation order. */
    int effectCount() const;
    /** @brief Returns an active effect by deterministic index, or nullptr. */
    Effect* effectAt(int index);
    /** @brief Returns active effect count for a subject. */
    int subjectCount(const std::string& subject) const;
    /** @brief Returns a subject effect by deterministic index, or nullptr. */
    Effect* subjectAt(const std::string& subject, int index);
    /** @brief Returns active subject effects containing a tag. */
    int taggedCount(const std::string& subject, const std::string& tag) const;
    /** @brief Returns a tagged subject effect by deterministic index. */
    Effect* taggedAt(const std::string& subject, const std::string& tag, int index);

    /** @brief Returns retained event count. */
    int eventCount() const;
    /** @brief Returns an event by deterministic sequence index, or nullptr. */
    EffectEvent* eventAt(int index);
    /** @brief Clears retained events without resetting their sequence counter. */
    void clearEvents();

private:
    using Store = std::deque<std::unique_ptr<Effect>>;
    Store::iterator findIterator(const std::string& id);
    void            emit(EffectEventKind kind, const Effect& effect, const std::string& reason = {});
    std::string     effectiveKey(const std::string& type, const std::string& stackKey) const;

    uint64_t                nextId_       = 1;
    uint64_t                nextSequence_ = 1;
    Store                   effects_;
    std::deque<EffectEvent> events_;
};

/** @brief Returns the stable lowercase name of a stack policy. */
std::string policyName(StackPolicy policy);
/** @brief Parses a lowercase stack policy name. */
bool parsePolicy(const std::string& name, StackPolicy& policy);
/** @brief Returns the stable lowercase name of an event kind. */
std::string eventKindName(EffectEventKind kind);

/** @brief Script module factory for generic effect containers. */
class Effects : public Module {
public:
    Module_REG(Effects);
    Effects()           = default;
    ~Effects() override = default;

    /** @brief Allocates a module-owned generic effect container. */
    static EffectContainer* newContainer();

private:
    std::vector<std::unique_ptr<EffectContainer>> containers_;
};

}  // namespace eve::effects
