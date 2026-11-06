#include "ModuleRegistryState.h"

#include <squirrel.h>
#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace eve {
namespace {

struct StackRestore {
    HSQUIRRELVM vm;
    SQInteger   top;
    ~StackRestore() { sq_settop(vm, top); }
};

ssq::Table cloneTable(const ssq::Table& source) {
    auto vm = source.getHandle();
    sq_pushobject(vm, source.getRaw());
    if (SQ_FAILED(sq_clone(vm, -1))) throw std::runtime_error("cannot stage plugin namespace");
    ssq::Object object(vm);
    sq_getstackobj(vm, -1, &object.getRaw());
    sq_addref(vm, &object.getRaw());
    sq_pop(vm, 2);
    return ssq::Table(object);
}

using Slots = std::unordered_map<std::string, ssq::Object>;

Slots slots(const ssq::Table& table) {
    auto  vm = table.getHandle();
    Slots result;
    sq_pushobject(vm, table.getRaw());
    sq_pushnull(vm);
    while (SQ_SUCCEEDED(sq_next(vm, -2))) {
        const SQChar* key = nullptr;
        if (SQ_SUCCEEDED(sq_getstring(vm, -2, &key))) {
            ssq::Object value(vm);
            sq_getstackobj(vm, -1, &value.getRaw());
            sq_addref(vm, &value.getRaw());
            result.emplace(key, std::move(value));
        }
        sq_pop(vm, 2);
    }
    sq_pop(vm, 2);
    return result;
}

void eraseSlot(const ssq::Table& table, const std::string& key) {
    auto vm = table.getHandle();
    sq_pushobject(vm, table.getRaw());
    sq_pushstring(vm, key.c_str(), static_cast<SQInteger>(key.size()));
    sq_deleteslot(vm, -2, SQFalse);
    sq_pop(vm, 1);
}

bool sameObject(const ssq::Object& a, const ssq::Object& b) {
    // Identity comparison must not invoke _cmp or compare incompatible types:
    // sq_cmp leaves its result undefined when the VM rejects a comparison.
    return a.getRaw()._type == b.getRaw()._type && a.getRaw()._unVal.raw == b.getRaw()._unVal.raw;
}

// SimpleSquirrel's native type map stores unrooted class handles. Keep every
// generation (including a partially bound candidate) rooted in the VM registry,
// so those handles and their native closure destructors stay valid until VM close.
void retainBindings(const ssq::Table& table) {
    auto vm = table.getHandle();
    sq_pushregistrytable(vm);
    sq_pushstring(vm, "eve.plugin.generations", -1);
    if (SQ_FAILED(sq_rawget(vm, -2))) {
        sq_pushstring(vm, "eve.plugin.generations", -1);
        sq_newarray(vm, 0);
        sq_newslot(vm, -3, SQFalse);
        sq_pushstring(vm, "eve.plugin.generations", -1);
        sq_rawget(vm, -2);
    }
    sq_pushobject(vm, table.getRaw());
    sq_arrayappend(vm, -2);
    sq_pop(vm, 2);
}

void restoreTypeMap(ssq::VM& machine, const Slots& before) {
    auto vm = machine.getHandle();
    for (const auto& [key, object] : before) {
        (void)key;
        sq_pushobject(vm, object.getRaw());
        SQUserPointer tag = nullptr;
        if (sq_gettype(vm, -1) == OT_CLASS && SQ_SUCCEEDED(sq_gettypetag(vm, -1, &tag)) && tag)
            machine.addClassObj(reinterpret_cast<size_t>(tag), object.getRaw());
        sq_pop(vm, 1);
    }
}

}  // namespace

Result<void> ModuleManager::publishPluginBindings() {
    auto& state   = *inst().state_;
    auto* machine = vm();
    if (!machine)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::Conflict, "plugin loading requires an attached script VM", {}, {}, "module"));
    auto         handle = machine->getHandle();
    StackRestore stack{handle, sq_gettop(handle)};
    Slots        before;
    try {
        auto live      = ssq::Table(machine->find("eve"));
        auto previous  = cloneTable(live);
        auto candidate = cloneTable(live);
        before         = slots(previous);
        retainBindings(previous);
        retainBindings(candidate);
        const auto&                    owner = state.plugin_registration_owner_;
        const auto                     found = state.plugin_binding_keys_.find(owner);
        const std::vector<std::string> oldKeys =
            found == state.plugin_binding_keys_.end() ? std::vector<std::string>{} : found->second;
        for (const auto& key : oldKeys) eraseSlot(candidate, key);
        for (const auto& name : state.plugin_registration_added_) {
            auto& info = state.registered_modules.at(name);
            if (info.exposer) info.exposer(candidate);
            info.exposed = true;
        }
        const auto               after = slots(candidate);
        std::vector<std::string> keys;
        for (const auto& [key, value] : after) {
            const auto existing = before.find(key);
            const bool owned    = std::find(oldKeys.begin(), oldKeys.end(), key) != oldKeys.end();
            if (existing != before.end() && !owned) {
                if (!sameObject(existing->second, value))
                    throw std::runtime_error("plugin binding conflicts with existing eve slot: " + key);
            } else {
                keys.push_back(key);
            }
        }
        // Publication only mutates eve's slots; existing references to the eve
        // table remain valid. No script or plugin callback runs during publication.
        state.plugin_binding_keys_[owner] = keys;
        for (const auto& key : oldKeys) eraseSlot(live, key);
        for (const auto& key : keys) live.set(key.c_str(), after.at(key));
        return Result<void>::success(Status::success(StatusCode::Applied));
    } catch (const std::exception& error) {
        restoreTypeMap(*machine, before);
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::Conflict, error.what(), {}, {}, "module"));
    } catch (...) {
        restoreTypeMap(*machine, before);
        return Result<void>::failure(
            Diagnostic::error(DiagnosticCode::Conflict, "plugin script binding threw", {}, {}, "module"));
    }
}

}  // namespace eve
