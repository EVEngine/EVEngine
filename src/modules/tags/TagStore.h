#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace eve::tags {

/** @brief Stable, game-defined identifier for an object carrying tags. */
using SubjectId = std::string;

/** @brief Immutable description of one tag or capability mutation. */
struct TagChangeEvent {
    std::uint64_t sequence = 0;
    std::string   action;
    SubjectId     subject;
    std::string   value;
};

/** @brief Deterministic storage for generic tags and capabilities. */
class TagStore {
public:
    /** @brief Adds a tag to a subject. @return True only when the set changed. */
    bool addTag(const SubjectId& subject, const std::string& tag);
    /** @brief Removes a tag from a subject. @return True only when the set changed. */
    bool removeTag(const SubjectId& subject, const std::string& tag);
    /** @brief Tests whether a subject has an exact tag. */
    bool hasTag(const SubjectId& subject, const std::string& tag) const;
    /** @brief Returns a subject's tags in lexical order. */
    std::vector<std::string> tagsOf(const SubjectId& subject) const;
    /** @brief Returns subjects carrying a tag in lexical order. */
    std::vector<SubjectId> subjectsWithTag(const std::string& tag) const;
    /** @brief Tests whether a subject has every requested tag; an empty request succeeds. */
    bool requireAllTags(const SubjectId& subject, const std::vector<std::string>& tags) const;
    /** @brief Tests whether a subject has any requested tag; an empty request fails. */
    bool requireAnyTag(const SubjectId& subject, const std::vector<std::string>& tags) const;

    /** @brief Adds a capability to a subject. @return True only when the set changed. */
    bool addCapability(const SubjectId& subject, const std::string& capability);
    /** @brief Removes a capability from a subject. @return True only when the set changed. */
    bool removeCapability(const SubjectId& subject, const std::string& capability);
    /** @brief Tests whether a subject has an exact capability. */
    bool hasCapability(const SubjectId& subject, const std::string& capability) const;
    /** @brief Returns a subject's capabilities in lexical order. */
    std::vector<std::string> capabilitiesOf(const SubjectId& subject) const;
    /** @brief Tests whether a subject has every requested capability; an empty request succeeds. */
    bool requireAllCapabilities(const SubjectId& subject, const std::vector<std::string>& capabilities) const;
    /** @brief Tests whether a subject has any requested capability; an empty request fails. */
    bool requireAnyCapability(const SubjectId& subject, const std::vector<std::string>& capabilities) const;

    /** @brief Removes all tags and capabilities belonging to one subject. */
    bool removeSubject(const SubjectId& subject);
    /** @brief Clears all state and resets event sequencing. */
    void clear();
    /** @brief Returns queued changes in deterministic sequence order. */
    const std::vector<TagChangeEvent>& events() const { return events_; }
    /** @brief Clears queued changes without changing tag state. */
    void clearEvents() { events_.clear(); }

private:
    using ValueSet = std::set<std::string>;
    using Index    = std::map<std::string, std::set<SubjectId>>;

    bool mutate(std::map<SubjectId, ValueSet>& values, Index* reverse, const SubjectId& subject,
                const std::string& value, bool add, const char* addedAction, const char* removedAction);
    static std::vector<std::string> values(const ValueSet* set);
    static bool                     requireAll(const ValueSet* held, const std::vector<std::string>& requested);
    static bool                     requireAny(const ValueSet* held, const std::vector<std::string>& requested);
    void                            emit(std::string action, SubjectId subject, std::string value);

    std::map<SubjectId, ValueSet> tags_;
    std::map<SubjectId, ValueSet> capabilities_;
    Index                         subjectsByTag_;
    std::vector<TagChangeEvent>   events_;
    std::uint64_t                 nextSequence_ = 1;
};

}  // namespace eve::tags
