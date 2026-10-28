#include "common/ReflectScript.h"

#include "common/Runtime.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstddef>
#include <string>

namespace eve {
namespace {

const char* scriptStateName(ScriptState state) {
    switch (state) {
        case ScriptState::Compiled: return "Compiled";
        case ScriptState::Running: return "Running";
        case ScriptState::Loaded: return "Loaded";
        case ScriptState::Unloading: return "Unloading";
        case ScriptState::Unloaded: return "Unloaded";
        case ScriptState::Failed: return "Failed";
    }
    return "Unknown";
}

/** Roots the value at the VM stack top and restores the stack to `top`. */
ssq::Object objectFromTop(HSQUIRRELVM vm, SQInteger top) {
    ssq::Object out(vm);
    sq_getstackobj(vm, -1, &out.getRaw());
    sq_addref(vm, &out.getRaw());
    sq_settop(vm, top);
    return out;
}

/** Null object rooted in the given VM (pushes null when returned). */
ssq::Object nullObject(HSQUIRRELVM vm) { return ssq::Object(vm); }

/** Reads a live member slot of any kind; null object when missing. */
ssq::Object readMemberValue(const ssq::Object& instance, const std::string& name) {
    if (instance.getType() != ssq::Type::INSTANCE || name.empty()) return nullObject(instance.getHandle());
    HSQUIRRELVM     vm  = instance.getHandle();
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, instance.getRaw());
    sq_pushstring(vm, name.c_str(), static_cast<SQInteger>(name.size()));
    if (SQ_FAILED(sq_get(vm, -2))) {
        sq_settop(vm, top);
        return nullObject(vm);
    }
    return objectFromTop(vm, top);
}

/** Reads one array element of a member; null object when missing/out of range. */
ssq::Object readArrayElement(const ssq::Object& instance, const std::string& name, size_t index) {
    if (instance.getType() != ssq::Type::INSTANCE || name.empty()) return nullObject(instance.getHandle());
    HSQUIRRELVM     vm  = instance.getHandle();
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, instance.getRaw());
    sq_pushstring(vm, name.c_str(), static_cast<SQInteger>(name.size()));
    if (SQ_FAILED(sq_get(vm, -2)) || sq_gettype(vm, -1) != OT_ARRAY) {
        sq_settop(vm, top);
        return nullObject(vm);
    }
    sq_pushinteger(vm, static_cast<SQInteger>(index));
    if (SQ_FAILED(sq_get(vm, -2))) {
        sq_settop(vm, top);
        return nullObject(vm);
    }
    return objectFromTop(vm, top);
}

/** Reads one table value of a member; null object when missing. */
ssq::Object readTableValue(const ssq::Object& instance, const std::string& name, const std::string& key) {
    if (instance.getType() != ssq::Type::INSTANCE || name.empty()) return nullObject(instance.getHandle());
    HSQUIRRELVM     vm  = instance.getHandle();
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, instance.getRaw());
    sq_pushstring(vm, name.c_str(), static_cast<SQInteger>(name.size()));
    if (SQ_FAILED(sq_get(vm, -2)) || sq_gettype(vm, -1) != OT_TABLE) {
        sq_settop(vm, top);
        return nullObject(vm);
    }
    sq_pushstring(vm, key.c_str(), static_cast<SQInteger>(key.size()));
    if (SQ_FAILED(sq_get(vm, -2))) {
        sq_settop(vm, top);
        return nullObject(vm);
    }
    return objectFromTop(vm, top);
}

/**
 * Converts a script scalar to a ReflectedValue for typed writes.
 * @return False for non-scalar script values (table/array/instance/null).
 */
bool convertScalar(const ssq::Object& value, ReflectedValue& out) {
    switch (value.getType()) {
        case ssq::Type::BOOL:
            out.kind    = ReflectedValueKind::Bool;
            out.boolean = value.toBool();
            return true;
        case ssq::Type::INTEGER:
            out.kind    = ReflectedValueKind::Integer;
            out.integer = value.toInt();
            return true;
        case ssq::Type::FLOAT:
            out.kind     = ReflectedValueKind::Float;
            out.floating = value.toFloat();
            return true;
        case ssq::Type::STRING:
            out.kind = ReflectedValueKind::String;
            out.text = value.toString();
            return true;
        default: return false;
    }
}

/** Member metadata table: name/type/method/attributes plus the live value. */
ssq::Table memberTable(ssq::VM& vm, const ReflectedMember& member, const ssq::Object* instance) {
    ssq::Table out = vm.newTable();
    out.set("name", member.name);
    out.set("type", std::string(ssq::typeToStr(member.type)));
    out.set("method", member.method);

    ssq::Table attributes = vm.newTable();
    for (const ReflectedAttribute& attribute : member.attributes)
        attributes.set(attribute.name.c_str(), attribute.value);
    out.set("attributes", attributes);

    if (instance && !member.method)
        out.set("value", readMemberValue(*instance, member.name));
    else
        out.set("value", nullObject(vm.getHandle()));
    return out;
}

/** Class metadata table: name/source/base plus an array of member tables. */
ssq::Table classTable(ssq::VM& vm, const ReflectedClass& cls) {
    ssq::Table out = vm.newTable();
    out.set("name", cls.name);
    out.set("source", cls.source);
    out.set("base", cls.base);

    ssq::Array members = vm.newArray();
    for (const ReflectedMember& member : cls.members) members.push(memberTable(vm, member, nullptr));
    out.set("members", members);
    return out;
}

/** Script info table: id/source/state/classes/error. */
ssq::Table scriptInfoTable(ssq::VM& vm, const ScriptInfo& info) {
    ssq::Table out = vm.newTable();
    out.set("id", static_cast<int>(info.id));
    out.set("source", info.source);
    out.set("state", std::string(scriptStateName(info.state)));
    out.set("error", info.error);

    ssq::Array classes = vm.newArray();
    for (const std::string& name : info.classes) classes.push(name);
    out.set("classes", classes);
    return out;
}

}  // namespace

void exposeReflection(Runtime& runtime, ssq::Table eveTable) {
    ssq::Table reflect = eveTable.addTable("reflect");

    reflect.addFunc("classes", [&runtime]() {
        ssq::Table out = runtime.vm().newTable();
        for (const ReflectedClass& cls : runtime.reflectedClasses())
            out.set(cls.name.c_str(), classTable(runtime.vm(), cls));
        return out;
    });

    reflect.addFunc("classInfo", [&runtime](std::string name) -> ssq::Object {
        const ReflectedClass* cls = runtime.reflectedClass(name);
        if (!cls) return nullObject(runtime.handle());
        return classTable(runtime.vm(), *cls);
    });

    reflect.addFunc("scan", [&runtime]() { return static_cast<int>(runtime.scanClasses()); });

    reflect.addFunc("createInstance", [&runtime](std::string name) { return runtime.createInstance(name); });

    reflect.addFunc("classNameOf", [&runtime](const ssq::Object& instance) { return runtime.classNameOf(instance); });

    reflect.addFunc("inspect", [&runtime](const ssq::Object& instance) {
        ssq::Array out = runtime.vm().newArray();
        if (instance.getType() != ssq::Type::INSTANCE) return out;
        for (const ReflectedMember& member : runtime.reflectInstance(instance))
            out.push(memberTable(runtime.vm(), member, &instance));
        return out;
    });

    reflect.addFunc("read",
                    [](const ssq::Object& instance, std::string name) { return readMemberValue(instance, name); });

    reflect.addFunc("write", [&runtime](const ssq::Object& instance, std::string name, const ssq::Object& value) {
        ReflectedValue converted;
        if (!convertScalar(value, converted)) return false;
        return runtime.writeProperty(instance, name, converted);
    });

    reflect.addFunc("readObject", [&runtime](const ssq::Object& instance, std::string name) {
        return runtime.readObjectProperty(instance, name);
    });

    reflect.addFunc("arraySize", [&runtime](const ssq::Object& instance, std::string name) {
        return static_cast<int>(runtime.arraySize(instance, name));
    });

    reflect.addFunc("arrayGet", [](const ssq::Object& instance, std::string name, int index) {
        return readArrayElement(instance, name, static_cast<size_t>(index));
    });

    reflect.addFunc("arraySet",
                    [&runtime](const ssq::Object& instance, std::string name, int index, const ssq::Object& value) {
                        ReflectedValue converted;
                        if (!convertScalar(value, converted)) return false;
                        return runtime.arraySet(instance, name, static_cast<size_t>(index), converted);
                    });

    reflect.addFunc("arrayAppend", [&runtime](const ssq::Object& instance, std::string name, const ssq::Object& value) {
        ReflectedValue converted;
        if (!convertScalar(value, converted)) return false;
        return runtime.arrayAppend(instance, name, converted);
    });

    reflect.addFunc("arrayRemove", [&runtime](const ssq::Object& instance, std::string name, int index) {
        return runtime.arrayRemove(instance, name, static_cast<size_t>(index));
    });

    reflect.addFunc("tableKeys", [&runtime](const ssq::Object& instance, std::string name) {
        ssq::Array out = runtime.vm().newArray();
        for (const std::string& key : runtime.tableKeys(instance, name)) out.push(key);
        return out;
    });

    reflect.addFunc("tableGet", [](const ssq::Object& instance, std::string name, std::string key) {
        return readTableValue(instance, name, key);
    });

    reflect.addFunc("tableSet", [&runtime](const ssq::Object& instance, std::string name, std::string key,
                                           const ssq::Object& value) {
        ReflectedValue converted;
        if (!convertScalar(value, converted)) return false;
        return runtime.tableSet(instance, name, key, converted);
    });

    reflect.addFunc("tableRemove", [&runtime](const ssq::Object& instance, std::string name, std::string key) {
        return runtime.tableRemove(instance, name, key);
    });

    reflect.addFunc("scripts", [&runtime]() {
        ssq::Array out = runtime.vm().newArray();
        for (const ScriptInfo& info : runtime.scripts()) out.push(scriptInfoTable(runtime.vm(), info));
        return out;
    });
}

}  // namespace eve
