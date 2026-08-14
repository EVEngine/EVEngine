#include "common/ECS.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstring>
#include <string>
#include <unordered_map>

namespace eve {
namespace {

// ---------------------------------------------------------------------------
// Script component registry (name → Squirrel class)
// ---------------------------------------------------------------------------

struct ScriptComponentReg {
    HSQUIRRELVM vm = nullptr;
    HSQOBJECT   cls;
    ScriptComponentReg() { sq_resetobject(&cls); }
    ScriptComponentReg(HSQUIRRELVM v, HSQOBJECT o) : vm(v), cls(o) {}
};

std::unordered_map<std::string, ScriptComponentReg>& scriptComponents() {
    static std::unordered_map<std::string, ScriptComponentReg> m;
    return m;
}

int& nextEntityId() {
    static int id = 1;
    return id;
}

int allocEntityId() { return nextEntityId()++; }

// C++ 实体类型（typeid(T*).hash_code()）→ 脚本 view() 收集函数。
std::unordered_map<size_t, CppEntityViewFn>& cppEntityViews() {
    static std::unordered_map<size_t, CppEntityViewFn> views;
    return views;
}

// ---------------------------------------------------------------------------
// Class helpers
// ---------------------------------------------------------------------------

bool isSubclassOf(HSQUIRRELVM vm, HSQOBJECT child, HSQOBJECT base) {
    if (child._type != OT_CLASS || base._type != OT_CLASS) return false;
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, child);
    for (int guard = 0; guard < 64; ++guard) {
        if (sq_gettype(vm, -1) != OT_CLASS) break;
        HSQOBJECT cur;
        sq_getstackobj(vm, -1, &cur);
        if (cur._unVal.pClass == base._unVal.pClass) {
            sq_settop(vm, top);
            return true;
        }
        if (SQ_FAILED(sq_getbase(vm, -1))) break;
        sq_remove(vm, -2);  // drop current, keep base
        if (sq_gettype(vm, -1) != OT_CLASS) break;
    }
    sq_settop(vm, top);
    return false;
}

/** Collect non-method members of a class into `out` (field defaults / statics). */
void collectClassFields(HSQUIRRELVM vm, HSQOBJECT clsObj, HSQOBJECT outTable) {
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, outTable);  // out
    sq_pushobject(vm, clsObj);    // out, cls
    sq_pushnull(vm);              // out, cls, iter
    while (SQ_SUCCEEDED(sq_next(vm, -2))) {
        // out, cls, iter, key, value
        if (sq_gettype(vm, -2) == OT_STRING) {
            const SQChar* key = nullptr;
            sq_getstring(vm, -2, &key);
            if (key && key[0] != '_') {
                const SQObjectType vt = sq_gettype(vm, -1);
                if (vt != OT_CLOSURE && vt != OT_NATIVECLOSURE && vt != OT_GENERATOR) {
                    sq_push(vm, -5);  // out
                    sq_push(vm, -3);  // key
                    sq_push(vm, -3);  // value
                    sq_rawset(vm, -3);
                    sq_pop(vm, 1);  // pop out
                }
            }
        }
        sq_pop(vm, 2);  // key, value
    }
    sq_settop(vm, top);
}

ssq::Table inspectClassFields(ssq::Object clsObj) {
    HSQUIRRELVM vm = clsObj.getHandle();
    ssq::Table out(vm);
    if (clsObj.getType() != ssq::Type::CLASS) return out;
    collectClassFields(vm, clsObj.getRaw(), out.getRaw());
    return out;
}

bool scriptIsSubclass(ssq::Object child, ssq::Object base) {
    if (child.getType() != ssq::Type::CLASS || base.getType() != ssq::Type::CLASS)
        return false;
    return isSubclassOf(child.getHandle(), child.getRaw(), base.getRaw());
}

ssq::Object getClassBase(ssq::Object cls) {
    HSQUIRRELVM vm = cls.getHandle();
    ssq::Object ret(vm);
    if (cls.getType() != ssq::Type::CLASS) return ret;
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, cls.getRaw());
    if (SQ_FAILED(sq_getbase(vm, -1)) || sq_gettype(vm, -1) != OT_CLASS) {
        sq_settop(vm, top);
        return ret;
    }
    sq_getstackobj(vm, -1, &ret.getRaw());
    sq_addref(vm, &ret.getRaw());
    sq_settop(vm, top);
    return ret;
}

void registerScriptComponent(const std::string& name, ssq::Object cls) {
    if (cls.getType() != ssq::Type::CLASS) return;
    HSQUIRRELVM vm = cls.getHandle();
    HSQOBJECT o = cls.getRaw();
    sq_addref(vm, &o);
    auto& m = scriptComponents();
    auto it = m.find(name);
    if (it != m.end()) {
        if (it->second.vm) sq_release(it->second.vm, &it->second.cls);
        it->second = ScriptComponentReg(vm, o);
    } else {
        m.emplace(name, ScriptComponentReg(vm, o));
    }
}

// ---------------------------------------------------------------------------
// Injected Squirrel runtime
// ---------------------------------------------------------------------------

const char* kEcsScript = R"SQ(
// Script ECS: define Component / Entity classes, create instances, view & System.
// Mirrors ECS.hpp's class + component model for Squirrel game logic.

if (!("Number" in eve)) eve.Number <- "number"
if (!("Boolean" in eve)) eve.Boolean <- "boolean"
if (!("String" in eve)) eve.String <- "string"

eve._ecsTypes <- {}   // EntityClass -> { cls, instances }

function eve::_ecsResolveMarker(v) {
    if (v == eve.Number || v == "number") return 0.0
    if (v == eve.Boolean || v == "boolean") return false
    if (v == eve.String || v == "string") return ""
    return v
}

function eve::_ecsIsComponentClass(obj) {
    if (typeof obj != "class") return false
    try { return eve.isSubclass(obj, eve.Component) } catch (e) { return false }
}

function eve::_ecsInstantiateComponent(compClass) {
    local c = compClass()
    local fields = eve.inspectClassFields(compClass)
    foreach (name, val in fields) {
        local cur = null
        try { cur = c[name] } catch (e) { cur = null }
        if (cur == eve.Number || cur == eve.Boolean || cur == eve.String
            || cur == "number" || cur == "boolean" || cur == "string") {
            c[name] = eve._ecsResolveMarker(cur)
        }
    }
    return c
}

function eve::_ecsCollectSlots(cls) {
    // { fieldName = ComponentClass } walking base classes (Entity → … → cls).
    local slots = {}
    local cur = cls
    local guard = 0
    while (cur != null && guard < 64) {
        if (!eve.isSubclass(cur, eve.Entity)) break

        local declared = null
        try { declared = cur.components } catch (e) { declared = null }
        if (typeof declared == "table") {
            foreach (k, v in declared) {
                if (!(k in slots) && eve._ecsIsComponentClass(v))
                    slots[k] <- v
            }
        }

        local fields = eve.inspectClassFields(cur)
        foreach (k, v in fields) {
            if (k == "components") continue
            if (!(k in slots) && eve._ecsIsComponentClass(v))
                slots[k] <- v
        }

        cur = eve.getClassBase(cur)
        guard += 1
    }
    return slots
}

function eve::_ecsEnsureType(cls) {
    if (cls in eve._ecsTypes) return eve._ecsTypes[cls]
    local info = { cls = cls, instances = [] }
    eve._ecsTypes[cls] <- info
    return info
}

function eve::_ecsRegisterInstance(entity, cls) {
    eve._ecsEnsureType(cls).instances.push(entity)
}

function eve::_ecsUnregisterInstance(entity, cls) {
    if (!(cls in eve._ecsTypes)) return
    local arr = eve._ecsTypes[cls].instances
    for (local i = arr.len() - 1; i >= 0; --i) {
        if (arr[i] == entity) { arr.remove(i); break }
    }
}

function eve::_ecsCollectInstances(cls, out) {
    foreach (key, info in eve._ecsTypes) {
        if (info.cls == cls || eve.isSubclass(info.cls, cls)) {
            foreach (e in info.instances) {
                if (e != null && e.isAlive()) out.push(e)
            }
        }
    }
}

eve.Component <- class {
    constructor() {}
}

eve.Entity <- class {
    _eid = 0
    _alive = false
    _etype = null
    _slots = null
    _comps = null

    constructor() {
        _eid = 0
        _alive = false
        _etype = null
        _slots = null
        _comps = null
    }

    static function create() {
        local e = this()
        e._etype = this
        e._eid = eve.allocEntityId()
        e._alive = true
        e._slots = eve._ecsCollectSlots(this)
        e._comps = {}
        foreach (name, compClass in e._slots) {
            local c = eve._ecsInstantiateComponent(compClass)
            e._comps[name] <- c
            try { e[name] = c } catch (ex) {}
        }
        eve._ecsRegisterInstance(e, this)
        return e
    }

    // Declare a component slot on this entity class (optional sugar).
    static function component(name, compClass) {
        if (!("components" in this) || typeof this.components != "table")
            this.components <- {}
        this.components[name] <- compClass
        return this
    }

    function getId() { return _eid }
    function isAlive() { return _alive }

    function getComponent(compClass) {
        if (_slots == null || _comps == null) return null
        foreach (name, cls in _slots) {
            if (cls == compClass) {
                if (name in _comps) return _comps[name]
                return null
            }
        }
        return null
    }

    function hasComponent(compClass) {
        return getComponent(compClass) != null
    }

    function destroy() {
        if (!_alive) return
        _alive = false
        if (_etype != null) eve._ecsUnregisterInstance(this, _etype)
    }
}

// Historical name from early game examples
eve.EntityContainer <- eve.Entity

eve.System <- class {
    _query = null

    constructor(query = null) {
        _query = query
    }

    function setQuery(query) { _query = query }

    function entities() {
        if (_query == null) return []
        if (typeof _query == "array") {
            local out = []
            foreach (q in _query) {
                foreach (e in eve.view(q)) out.push(e)
            }
            return out
        }
        return eve.view(_query)
    }

    function update(dt) {}
}

// GPU-backed System: pack float component fields → SSBO → compute shader → unpack.
// Requires eve.Gpgpu / eve.EcsShaderSystem (gpgpu module). Push constants:
//   pc.data[0] = dt, pc.data[1] = entityCount. Each bindFields() maps one binding.
eve.ShaderSystem <- class extends eve.System {
    _gpu = null
    _backend = null
    _bindings = null
    _readback = true
    _localSize = 64

    constructor(query = null, gpu = null, glsl = null, localSize = 64) {
        base.constructor(query)
        _bindings = []
        _readback = true
        _localSize = localSize
        if (gpu != null) setGpgpu(gpu)
        if (gpu != null && glsl != null) setShaderSource(glsl)
    }

    function setGpgpu(gpu) {
        _gpu = gpu
        if (_backend == null) {
            if (!("EcsShaderSystem" in eve))
                throw "eve.ShaderSystem requires gpgpu module (eve.EcsShaderSystem)"
            _backend = eve.EcsShaderSystem()
        }
        _backend.setGpgpu(gpu)
        _backend.setLocalSize(_localSize)
        return this
    }

    function setShaderSource(glsl) {
        if (_backend == null) throw "eve.ShaderSystem.setShaderSource: call setGpgpu first"
        _backend.setShaderSource(glsl)
        return this
    }

    function setLocalSize(n) {
        _localSize = n
        if (_backend != null) _backend.setLocalSize(n)
        return this
    }

    function setReadback(enabled) {
        _readback = enabled
        return this
    }

    // binding: SSBO index; slot: entity component field name; fields: ["x","y",...]
    function bindFields(binding, slot, fields) {
        _bindings.push({ binding = binding, slot = slot, fields = fields })
        return this
    }

    function setFloat(index, value) {
        if (_backend != null) _backend.setFloat(index, value)
        return this
    }

    function getBackend() { return _backend }

    function update(dt) {
        if (_backend == null || _gpu == null) return
        if (!("packEcsFloats" in eve) || !("unpackEcsFloats" in eve)) return

        local ents = entities()
        local n = ents.len()
        if (n <= 0) return

        foreach (b in _bindings) {
            local floatsPer = b.fields.len()
            if (floatsPer <= 0) continue
            local buf = _backend.ensureBuffer(b.binding, n * floatsPer)
            eve.packEcsFloats(ents, b.slot, b.fields, buf)
        }

        _backend.dispatch(n, dt)

        if (_readback) {
            foreach (b in _bindings) {
                local floatsPer = b.fields.len()
                if (floatsPer <= 0) continue
                local buf = _backend.getBuffer(b.binding)
                eve.unpackEcsFloats(ents, b.slot, b.fields, buf, n)
            }
        }
    }
}

function eve::view(entityClass) {
    local out = []
    if (entityClass == null) return out
    eve._ecsCollectInstances(entityClass, out)
    if ("_cppCollect" in eve) eve._cppCollect(entityClass, out)
    return out
}

function eve::ecsReady() {
    return ("Entity" in eve) && ("Component" in eve) && ("System" in eve)
}
)SQ";

void injectEcsScript(ssq::Table& eveTable) {
    HSQUIRRELVM vm = eveTable.getHandle();
    const SQInteger top = sq_gettop(vm);
    if (SQ_FAILED(sq_compilebuffer(vm, kEcsScript,
                                   static_cast<SQInteger>(std::strlen(kEcsScript)),
                                   "ecs.nut", SQTrue))) {
        sq_settop(vm, top);
        return;
    }
    sq_pushroottable(vm);
    sq_call(vm, 1, SQFalse, SQTrue);
    sq_settop(vm, top);
}

/**
 * Script hook for eve.view(): walk the script class chain (cls → base → …) and,
 * at the first class registered via registerCppEntityView(), invoke its collector
 * to append the matching C++ entities to `out`.
 */
void cppCollect(ssq::Class cls, ssq::Array out) {
    HSQUIRRELVM vm = cls.getHandle();
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, cls.getRaw());
    for (int guard = 0; guard < 64; ++guard) {
        if (sq_gettype(vm, -1) != OT_CLASS)
            break;
        HSQOBJECT cur;
        sq_getstackobj(vm, -1, &cur);
        SQUserPointer tag = nullptr;
        if (SQ_SUCCEEDED(sq_getobjtypetag(&cur, &tag))) {
            auto it = cppEntityViews().find(reinterpret_cast<size_t>(tag));
            if (it != cppEntityViews().end()) {
                sq_settop(vm, top);
                it->second(out);
                return;
            }
        }
        if (SQ_FAILED(sq_getbase(vm, -1)))
            break;
        sq_remove(vm, -2);  // drop current class, keep base
    }
    sq_settop(vm, top);
}

}  // namespace

void exposeECS(ssq::Table& table) {
    table.set("Number", std::string("number"));
    table.set("Boolean", std::string("boolean"));
    table.set("String", std::string("string"));
    // ecsReady / view / Entity / Component are provided by the injected script.

    table.addFunc("inspectClassFields", inspectClassFields);
    table.addFunc("isSubclass", scriptIsSubclass);
    table.addFunc("getClassBase", getClassBase);
    table.addFunc("allocEntityId", allocEntityId);
    table.addFunc("component", [](std::string name, ssq::Object cls) {
        registerScriptComponent(name, cls);
    });
    table.addFunc("_cppCollect", std::function<void(ssq::Class, ssq::Array)>(cppCollect));

    injectEcsScript(table);
}

void registerCppEntityView(size_t typeHash, CppEntityViewFn fn) {
    cppEntityViews()[typeHash] = std::move(fn);
}

void exposeECSToVM(ssq::VM& vm) {
    try {
        ssq::Table eveTable(vm.find("eve"));
        exposeECS(eveTable);
    } catch (...) {
    }
}

}  // namespace eve
