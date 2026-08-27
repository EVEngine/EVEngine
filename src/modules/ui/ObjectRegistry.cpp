#include "ui/ObjectRegistry.h"

#include "common/Module.h"
#include "common/Runtime.h"
#include "common/SquirrelOwnership.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace eve::ui {
namespace {

template <typename T>
eve::Result<T> registryFailure(eve::DiagnosticCode code, std::string message) {
    return eve::Result<T>::failure(eve::Diagnostic::error(code, std::move(message)));
}

}  // namespace

ObjectRegistry& ObjectRegistry::instance() {
    static ObjectRegistry registry;
    return registry;
}

ObjectRegistry::Slot* ObjectRegistry::slot(ObjectHandle handle) noexcept {
    if (!handle.isValid() || handle.index() >= slots_.size()) return nullptr;
    return &slots_[handle.index()];
}

const ObjectRegistry::Slot* ObjectRegistry::slot(ObjectHandle handle) const noexcept {
    if (!handle.isValid() || handle.index() >= slots_.size()) return nullptr;
    return &slots_[handle.index()];
}

eve::Result<ObjectHandle> ObjectRegistry::create(const std::string& className) {
    if (className.empty()) {
        return registryFailure<ObjectHandle>(eve::DiagnosticCode::InvalidArgument,
                                             "UI object class name must not be empty");
    }

    Runtime* runtime = ModuleManager::runtime();
    if (!runtime) {
        return registryFailure<ObjectHandle>(eve::DiagnosticCode::Failed,
                                             "UI object registry requires an active Runtime");
    }

    try {
        return registerObject(className, runtime->createInstance(className));
    } catch (const std::exception& error) {
        return registryFailure<ObjectHandle>(
            eve::DiagnosticCode::Failed,
            std::string("UI object construction failed: ") + error.what());
    } catch (...) {
        return registryFailure<ObjectHandle>(eve::DiagnosticCode::Failed,
                                             "UI object construction failed");
    }
}

eve::Result<ObjectHandle> ObjectRegistry::registerObject(const std::string& className,
                                                         const ssq::Object& object,
                                                         const std::string& label) {
    std::uint32_t allocatedIndex = ObjectHandle::invalidIndex;
    bool appendedSlot = false;
    try {
        if (object.getType() != ssq::Type::INSTANCE) {
            return registryFailure<ObjectHandle>(
                eve::DiagnosticCode::InvalidArgument,
                "UI object registry accepts only script instances");
        }

        std::string group = className;
        if (group.empty()) {
            Runtime* runtime = ModuleManager::runtime();
            if (!runtime) {
                return registryFailure<ObjectHandle>(
                    eve::DiagnosticCode::Failed,
                    "UI object registry requires an active Runtime to derive the class");
            }
            group = runtime->classNameOf(object);
            if (group.empty()) {
                return registryFailure<ObjectHandle>(
                    eve::DiagnosticCode::InvalidArgument,
                    "UI object registry could not derive a script class name");
            }
        }

        if (!freeSlots_.empty()) {
            allocatedIndex = freeSlots_.back();
        } else {
            if (slots_.size() >= ObjectHandle::invalidIndex) {
                return registryFailure<ObjectHandle>(
                    eve::DiagnosticCode::Failed,
                    "UI object registry exhausted its slot index space");
            }
            allocatedIndex = static_cast<std::uint32_t>(slots_.size());
            slots_.emplace_back();
            appendedSlot = true;
        }

        Slot& target = slots_[allocatedIndex];
        if (target.retired || target.entry.has_value()) {
            if (appendedSlot) slots_.pop_back();
            return registryFailure<ObjectHandle>(
                eve::DiagnosticCode::InvariantViolation,
                "UI object registry selected an occupied or retired slot");
        }

        const ObjectHandle handle(allocatedIndex, target.generation);
        auto ownedObject = eve::script::ownSquirrelObject(
            eve::script::borrowSquirrelObject(object));
        if (!ownedObject) return eve::Result<ObjectHandle>::failure(ownedObject.status());
        ObjectEntry candidate;
        candidate.handle = handle;
        candidate.className = group;
        candidate.object = std::move(ownedObject).takeValue().release();
        candidate.label = label.empty() ? group + " #" +
                                             std::to_string(count(group) + 1)
                                       : label;
        target.entry.emplace(std::move(candidate));

        bool insertedClass = false;
        try {
            auto [classIt, inserted] = byClass_.try_emplace(group);
            insertedClass = inserted;
            classIt->second.push_back(handle);
        } catch (...) {
            if (insertedClass) byClass_.erase(group);
            target.entry.reset();
            throw;
        }

        // The free-list entry is consumed only after all potentially-throwing
        // registration work has completed.
        if (!freeSlots_.empty() && freeSlots_.back() == allocatedIndex)
            freeSlots_.pop_back();
        return eve::Result<ObjectHandle>::success(handle);
    } catch (const std::exception& error) {
        if (appendedSlot && !slots_.empty() && allocatedIndex + 1 == slots_.size() &&
            !slots_.back().entry.has_value()) {
            slots_.pop_back();
        }
        return registryFailure<ObjectHandle>(
            eve::DiagnosticCode::Failed,
            std::string("UI object registration failed: ") + error.what());
    } catch (...) {
        if (appendedSlot && !slots_.empty() && allocatedIndex + 1 == slots_.size() &&
            !slots_.back().entry.has_value()) {
            slots_.pop_back();
        }
        return registryFailure<ObjectHandle>(eve::DiagnosticCode::Failed,
                                             "UI object registration failed");
    }
}

eve::Result<void> ObjectRegistry::unregister(ObjectHandle handle) {
    if (!handle.isValid()) {
        return registryFailure<void>(eve::DiagnosticCode::InvalidArgument,
                                     "cannot unregister an invalid UI object handle");
    }

    Slot* target = slot(handle);
    if (!target) {
        return registryFailure<void>(eve::DiagnosticCode::NotFound,
                                     "UI object handle index is outside the registry");
    }
    if (!target->entry.has_value() || target->generation != handle.generation()) {
        return registryFailure<void>(eve::DiagnosticCode::StaleHandle,
                                     "UI object handle is stale");
    }

    auto classIt = byClass_.find(target->entry->className);
    if (classIt == byClass_.end()) {
        return registryFailure<void>(eve::DiagnosticCode::InvariantViolation,
                                     "UI object class index is missing");
    }
    auto& classEntries = classIt->second;
    const auto handleIt = std::find(classEntries.begin(), classEntries.end(), handle);
    if (handleIt == classEntries.end()) {
        return registryFailure<void>(eve::DiagnosticCode::InvariantViolation,
                                     "UI object class index does not contain its handle");
    }

    const auto next = ObjectHandle::nextGeneration(target->generation);
    if (next) {
        try {
            // Reserve the reusable-slot bookkeeping before making the live
            // entry unreachable. The remaining operations are noexcept.
            freeSlots_.push_back(handle.index());
        } catch (const std::exception& error) {
            return registryFailure<void>(
                eve::DiagnosticCode::Failed,
                std::string("UI object slot release failed: ") + error.what());
        } catch (...) {
            return registryFailure<void>(eve::DiagnosticCode::Failed,
                                         "UI object slot release failed");
        }
    }

    classEntries.erase(handleIt);
    if (classEntries.empty()) byClass_.erase(classIt);
    target->entry.reset();
    if (next) {
        target->generation = *next;
    } else {
        target->retired = true;
    }
    return eve::Result<void>::success(eve::Status::success(eve::StatusCode::Applied));
}

void ObjectRegistry::clear(const std::string& className) {
    const auto found = byClass_.find(className);
    if (found == byClass_.end()) return;

    const std::vector<ObjectHandle> handles = found->second;
    for (const ObjectHandle handle : handles) {
        unregister(handle).ignore("clear UI object class");
    }
}

void ObjectRegistry::clearAll() {
    std::vector<ObjectHandle> handles;
    for (const auto& [className, classHandles] : byClass_) {
        (void)className;
        handles.insert(handles.end(), classHandles.begin(), classHandles.end());
    }
    for (const ObjectHandle handle : handles) {
        unregister(handle).ignore("clear all UI objects");
    }
}

std::vector<std::string> ObjectRegistry::classNames() const {
    std::vector<std::string> names;
    names.reserve(byClass_.size());
    for (const auto& pair : byClass_) names.push_back(pair.first);
    return names;
}

std::vector<ObjectEntry> ObjectRegistry::entries(const std::string& className) const {
    const auto found = byClass_.find(className);
    if (found == byClass_.end()) return {};

    std::vector<ObjectEntry> result;
    result.reserve(found->second.size());
    for (const ObjectHandle handle : found->second) {
        if (const ObjectEntry* value = entry(handle)) result.push_back(*value);
    }
    return result;
}

const ObjectEntry* ObjectRegistry::entry(ObjectHandle handle) const noexcept {
    const Slot* target = slot(handle);
    if (!target || !target->entry.has_value() || target->generation != handle.generation())
        return nullptr;
    return &*target->entry;
}

bool ObjectRegistry::isStale(ObjectHandle handle) const noexcept {
    if (!handle.isValid()) return false;
    const Slot* target = slot(handle);
    return !target || !target->entry.has_value() || target->generation != handle.generation();
}

size_t ObjectRegistry::count(const std::string& className) const {
    const auto found = byClass_.find(className);
    return found == byClass_.end() ? 0 : found->second.size();
}

}  // namespace eve::ui
