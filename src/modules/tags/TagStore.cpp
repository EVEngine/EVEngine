#include "tags/TagStore.h"

#include <algorithm>
#include <utility>

namespace eve::tags {

std::vector<std::string> TagStore::values(const ValueSet* set) {
    return set ? std::vector<std::string>(set->begin(), set->end()) : std::vector<std::string>{};
}

bool TagStore::requireAll(const ValueSet* held, const std::vector<std::string>& requested) {
    return std::all_of(requested.begin(), requested.end(),
                       [held](const auto& value) { return held && held->contains(value); });
}

bool TagStore::requireAny(const ValueSet* held, const std::vector<std::string>& requested) {
    return held &&
           std::any_of(requested.begin(), requested.end(), [held](const auto& value) { return held->contains(value); });
}

void TagStore::emit(std::string action, SubjectId subject, std::string value) {
    events_.push_back({nextSequence_++, std::move(action), std::move(subject), std::move(value)});
}

bool TagStore::mutate(std::map<SubjectId, ValueSet>& valuesBySubject, Index* reverse, const SubjectId& subject,
                      const std::string& value, bool add, const char* addedAction, const char* removedAction) {
    if (subject.empty() || value.empty()) return false;
    auto found = valuesBySubject.find(subject);
    if (add) {
        auto& values = valuesBySubject[subject];
        if (!values.insert(value).second) return false;
        if (reverse) (*reverse)[value].insert(subject);
        emit(addedAction, subject, value);
        return true;
    }
    if (found == valuesBySubject.end() || found->second.erase(value) == 0) return false;
    if (found->second.empty()) valuesBySubject.erase(found);
    if (reverse) {
        auto indexed = reverse->find(value);
        if (indexed != reverse->end()) {
            indexed->second.erase(subject);
            if (indexed->second.empty()) reverse->erase(indexed);
        }
    }
    emit(removedAction, subject, value);
    return true;
}

bool TagStore::addTag(const SubjectId& subject, const std::string& tag) {
    return mutate(tags_, &subjectsByTag_, subject, tag, true, "tag_added", "tag_removed");
}
bool TagStore::removeTag(const SubjectId& subject, const std::string& tag) {
    return mutate(tags_, &subjectsByTag_, subject, tag, false, "tag_added", "tag_removed");
}
bool TagStore::hasTag(const SubjectId& subject, const std::string& tag) const {
    const auto found = tags_.find(subject);
    return found != tags_.end() && found->second.contains(tag);
}
bool TagStore::hasTagMatching(const SubjectId& subject, const std::string& tag, GameplayTagMatch match) const {
    const auto found = tags_.find(subject);
    return found != tags_.end() && std::any_of(found->second.begin(), found->second.end(), [&](const auto& held) {
               return gameplayTagMatches(held, tag, match);
           });
}
std::vector<std::string> TagStore::tagsOf(const SubjectId& subject) const {
    const auto found = tags_.find(subject);
    return values(found == tags_.end() ? nullptr : &found->second);
}
std::vector<SubjectId> TagStore::subjectsWithTag(const std::string& tag) const {
    const auto found = subjectsByTag_.find(tag);
    return found == subjectsByTag_.end() ? std::vector<SubjectId>{}
                                         : std::vector<SubjectId>(found->second.begin(), found->second.end());
}
std::vector<SubjectId> TagStore::subjectsWithTagMatching(const std::string& tag, GameplayTagMatch match) const {
    if (match == GameplayTagMatch::Exact) return subjectsWithTag(tag);
    std::set<SubjectId> matches;
    for (const auto& [held, subjects] : subjectsByTag_)
        if (gameplayTagMatches(held, tag, match)) matches.insert(subjects.begin(), subjects.end());
    return {matches.begin(), matches.end()};
}
bool TagStore::requireAllTags(const SubjectId& subject, const std::vector<std::string>& requested) const {
    const auto found = tags_.find(subject);
    return requireAll(found == tags_.end() ? nullptr : &found->second, requested);
}
bool TagStore::requireAnyTag(const SubjectId& subject, const std::vector<std::string>& requested) const {
    const auto found = tags_.find(subject);
    return requireAny(found == tags_.end() ? nullptr : &found->second, requested);
}
bool TagStore::matchesAllTags(const SubjectId& subject, const std::vector<std::string>& requested,
                              GameplayTagMatch match) const {
    return std::all_of(requested.begin(), requested.end(),
                       [&](const auto& query) { return hasTagMatching(subject, query, match); });
}
bool TagStore::matchesAnyTag(const SubjectId& subject, const std::vector<std::string>& requested,
                             GameplayTagMatch match) const {
    return std::any_of(requested.begin(), requested.end(),
                       [&](const auto& query) { return hasTagMatching(subject, query, match); });
}

bool TagStore::addCapability(const SubjectId& subject, const std::string& capability) {
    return mutate(capabilities_, nullptr, subject, capability, true, "capability_added", "capability_removed");
}
bool TagStore::removeCapability(const SubjectId& subject, const std::string& capability) {
    return mutate(capabilities_, nullptr, subject, capability, false, "capability_added", "capability_removed");
}
bool TagStore::hasCapability(const SubjectId& subject, const std::string& capability) const {
    const auto found = capabilities_.find(subject);
    return found != capabilities_.end() && found->second.contains(capability);
}
std::vector<std::string> TagStore::capabilitiesOf(const SubjectId& subject) const {
    const auto found = capabilities_.find(subject);
    return values(found == capabilities_.end() ? nullptr : &found->second);
}
bool TagStore::requireAllCapabilities(const SubjectId& subject, const std::vector<std::string>& requested) const {
    const auto found = capabilities_.find(subject);
    return requireAll(found == capabilities_.end() ? nullptr : &found->second, requested);
}
bool TagStore::requireAnyCapability(const SubjectId& subject, const std::vector<std::string>& requested) const {
    const auto found = capabilities_.find(subject);
    return requireAny(found == capabilities_.end() ? nullptr : &found->second, requested);
}

bool TagStore::removeSubject(const SubjectId& subject) {
    const auto tags         = tagsOf(subject);
    const auto capabilities = capabilitiesOf(subject);
    for (const auto& tag : tags) removeTag(subject, tag);
    for (const auto& capability : capabilities) removeCapability(subject, capability);
    if (tags.empty() && capabilities.empty()) return false;
    emit("subject_removed", subject, {});
    return true;
}

void TagStore::clear() {
    tags_.clear();
    capabilities_.clear();
    subjectsByTag_.clear();
    events_.clear();
    nextSequence_ = 1;
}

}  // namespace eve::tags
