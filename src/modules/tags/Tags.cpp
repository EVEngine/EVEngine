#include "tags/Tags.h"

#include <simplesquirrel/simplesquirrel.hpp>

namespace eve::tags {
namespace {

std::string valueAt(const std::vector<std::string>& values, int index) {
    return index < 0 || static_cast<std::size_t>(index) >= values.size() ? std::string{} : values[index];
}

const TagChangeEvent* eventAt(const TagStore& store, int index) {
    const auto& events = store.events();
    return index < 0 || static_cast<std::size_t>(index) >= events.size() ? nullptr : &events[index];
}

std::vector<std::string> strings(ssq::Array array) {
    std::vector<std::string> result;
    result.reserve(array.size());
    for (std::size_t i = 0; i < array.size(); ++i) result.push_back(array.get<std::string>(i));
    return result;
}

bool requireAllTags(Tags* self, const std::string& subject, ssq::Array requested) {
    return self->store().requireAllTags(subject, strings(requested));
}
bool requireAnyTag(Tags* self, const std::string& subject, ssq::Array requested) {
    return self->store().requireAnyTag(subject, strings(requested));
}
bool requireAllCapabilities(Tags* self, const std::string& subject, ssq::Array requested) {
    return self->store().requireAllCapabilities(subject, strings(requested));
}
bool requireAnyCapability(Tags* self, const std::string& subject, ssq::Array requested) {
    return self->store().requireAnyCapability(subject, strings(requested));
}

}  // namespace

Module_IMPL(Tags, new Tags());

bool        Tags::add(const std::string& subject, const std::string& tag) { return store_.addTag(subject, tag); }
bool        Tags::remove(const std::string& subject, const std::string& tag) { return store_.removeTag(subject, tag); }
bool        Tags::has(const std::string& subject, const std::string& tag) const { return store_.hasTag(subject, tag); }
int         Tags::count(const std::string& subject) const { return static_cast<int>(store_.tagsOf(subject).size()); }
std::string Tags::at(const std::string& subject, int index) const { return valueAt(store_.tagsOf(subject), index); }
int Tags::subjectCount(const std::string& tag) const { return static_cast<int>(store_.subjectsWithTag(tag).size()); }
std::string Tags::subjectAt(const std::string& tag, int index) const {
    return valueAt(store_.subjectsWithTag(tag), index);
}

bool Tags::addCapability(const std::string& subject, const std::string& capability) {
    return store_.addCapability(subject, capability);
}
bool Tags::removeCapability(const std::string& subject, const std::string& capability) {
    return store_.removeCapability(subject, capability);
}
bool Tags::hasCapability(const std::string& subject, const std::string& capability) const {
    return store_.hasCapability(subject, capability);
}
int Tags::capabilityCount(const std::string& subject) const {
    return static_cast<int>(store_.capabilitiesOf(subject).size());
}
std::string Tags::capabilityAt(const std::string& subject, int index) const {
    return valueAt(store_.capabilitiesOf(subject), index);
}

bool Tags::removeSubject(const std::string& subject) { return store_.removeSubject(subject); }
void Tags::clear() { store_.clear(); }
int  Tags::eventCount() const { return static_cast<int>(store_.events().size()); }
int  Tags::eventSequence(int index) const {
    const auto* event = eventAt(store_, index);
    return event ? static_cast<int>(event->sequence) : 0;
}
std::string Tags::eventAction(int index) const {
    const auto* event = eventAt(store_, index);
    return event ? event->action : std::string{};
}
std::string Tags::eventSubject(int index) const {
    const auto* event = eventAt(store_, index);
    return event ? event->subject : std::string{};
}
std::string Tags::eventValue(int index) const {
    const auto* event = eventAt(store_, index);
    return event ? event->value : std::string{};
}
void Tags::clearEvents() { store_.clearEvents(); }

void Tags::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Tags::create, false);
    expose(cls);
}

void Tags::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Tags::getName);
    cls.addFunc("add", &Tags::add);
    cls.addFunc("remove", &Tags::remove);
    cls.addFunc("has", &Tags::has);
    cls.addFunc("count", &Tags::count);
    cls.addFunc("at", &Tags::at);
    cls.addFunc("subjectCount", &Tags::subjectCount);
    cls.addFunc("subjectAt", &Tags::subjectAt);
    cls.addFunc("requireAll", requireAllTags);
    cls.addFunc("requireAny", requireAnyTag);
    cls.addFunc("addCapability", &Tags::addCapability);
    cls.addFunc("removeCapability", &Tags::removeCapability);
    cls.addFunc("hasCapability", &Tags::hasCapability);
    cls.addFunc("capabilityCount", &Tags::capabilityCount);
    cls.addFunc("capabilityAt", &Tags::capabilityAt);
    cls.addFunc("requireAllCapabilities", requireAllCapabilities);
    cls.addFunc("requireAnyCapability", requireAnyCapability);
    cls.addFunc("removeSubject", &Tags::removeSubject);
    cls.addFunc("clear", &Tags::clear);
    cls.addFunc("eventCount", &Tags::eventCount);
    cls.addFunc("eventSequence", &Tags::eventSequence);
    cls.addFunc("eventAction", &Tags::eventAction);
    cls.addFunc("eventSubject", &Tags::eventSubject);
    cls.addFunc("eventValue", &Tags::eventValue);
    cls.addFunc("clearEvents", &Tags::clearEvents);
}

}  // namespace eve::tags
