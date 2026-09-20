#include "common/ECS.h"
#include "common/ScriptComponentStore.h"
#include "common/ScriptError.h"
#include "common/SquirrelBinding.h"

#include <simplesquirrel/simplesquirrel.hpp>

// clang-format off
// Squirrel internals: recovering a component schema's *declaration order* needs the class
// member table and the field-index encoding (`_isfield` / `_member_idx`), which the public
// API cannot express (it only exposes the hash-ordered member iteration of §10.17).
// The order is load-bearing -- `sqpcheader.h` must come first and clang-format would sort
// `sqclass.h` ahead of it -- and these have to follow `<simplesquirrel/simplesquirrel.hpp>`,
// which brings in the public Squirrel types.
#include "sqpcheader.h"
#include "sqstring.h"
#include "sqtable.h"
#include "sqclass.h"
// clang-format on

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace eve {
namespace {

// ---------------------------------------------------------------------------
// Script component registry (name + version → Squirrel class, with generations)
// ---------------------------------------------------------------------------

struct ScriptComponentEntry {
    HSQUIRRELVM vm = nullptr;
    HSQOBJECT   cls;
    Generation  generation;
    /** 声明顺序的字段名：schema，也是第 2 步摘字段槽位时的清单。 */
    std::vector<std::string> fields;
    ScriptComponentEntry() { sq_resetobject(&cls); }
    ScriptComponentEntry(HSQUIRRELVM v, HSQOBJECT o, Generation g, std::vector<std::string> f)
        : vm(v), cls(o), generation(g), fields(std::move(f)) {}
};

/** Keyed by (name, version) rather than by class object: hot reload replaces the class
 *  but keeps the identity, and identity is what migration and the engine-side store
 *  address. Ordered so enumeration is deterministic. */
using ScriptComponentKey = std::pair<std::string, std::uint64_t>;
using ScriptComponentMap = std::map<ScriptComponentKey, ScriptComponentEntry>;

ScriptComponentMap& currentScriptComponents() {
    static ScriptComponentMap m;
    return m;
}

/** Retired generations, metadata only: the replaced class object is kept alive by the
 *  instances that still reference it, so the registry does not hold a second reference. */
std::vector<ScriptComponentRecord>& retiredScriptComponents() {
    static std::vector<ScriptComponentRecord> v;
    return v;
}

int& nextEntityId() {
    static int id = 1;
    return id;
}

int allocEntityId() { return nextEntityId()++; }

// 当前 VM 的 Squirrel 类对象 → 脚本 view() 收集函数。
std::unordered_map<const void*, CppEntityViewFn>& cppEntityViews() {
    static std::unordered_map<const void*, CppEntityViewFn> views;
    return views;
}

std::vector<std::function<void(ssq::Table&)>>& postEcsHooks() {
    static std::vector<std::function<void(ssq::Table&)>> hooks;
    return hooks;
}

bool& ecsScriptInjected() {
    static bool injected = false;
    return injected;
}

size_t& postEcsHooksFlushed() {
    static size_t n = 0;
    return n;
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

/**
 * @brief 类声明的**实例字段**名，按声明顺序。
 *
 * 不用 `sq_next` 遍历取值：那是成员表的哈希顺序，增删一个字段就会让无关字段换位置（§10.17）。
 * Squirrel 内部把字段序号编进 `_members` 的值（`_isfield` / `_member_idx`），而 `_defaultvalues`
 * 是按声明顺序追加的，因此"按字段序号排序"就还原了声明顺序。方法成员带的是方法序号，被
 * `_isfield` 过滤掉；`_` 前缀的成员（`_get`/`_set`/构造器等）不算字段。
 *
 * @remarks **不需要**沿基类链递归：Squirrel 建立派生类时会把基类的成员一并放进派生类自己的
 *          `_members`（实测 `class EnemyStats extends BaseStats` 会同时列出 `hp` 与 `speed`），
 *          递归反而会把基类字段重复计一次。
 */
std::vector<std::string> declaredInstanceFields(HSQOBJECT clsObj) {
    std::vector<std::pair<SQInteger, std::string>> ordered;
    SQClass*                                       klass = _class(clsObj);
    if (klass == nullptr || klass->_members == nullptr) return {};

    SQObjectPtr key;
    SQObjectPtr value;
    SQObjectPtr refpos;  // `Next` 的游标是它自己返回的整数下标，不是 key（传 key 会命中 TranslateIndex 的断言）
    SQInteger   index = 0;
    while ((index = klass->_members->Next(false, refpos, key, value)) != -1) {
        refpos = SQObjectPtr(index);
        if (sq_type(key) != OT_STRING || !_isfield(value)) continue;
        const SQChar* name = _stringval(key);
        if (name == nullptr || name[0] == '_') continue;
        ordered.emplace_back(static_cast<SQInteger>(_member_idx(value)), std::string(name));
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    std::vector<std::string> out;
    out.reserve(ordered.size());
    for (auto& entry : ordered) out.push_back(std::move(entry.second));
    return out;
}

ssq::Table inspectClassFields(ssq::Object clsObj) {
    HSQUIRRELVM vm = clsObj.getHandle();
    ssq::Table out(vm);
    if (clsObj.getType() != ssq::Type::CLASS) return out;
    collectClassFields(vm, clsObj.getRaw(), out.getRaw());
    return out;
}

/** 读类里某个字段的默认值（声明时写的那个值就是 schema 默认值）。 */
Result<Value> classFieldDefault(HSQUIRRELVM vm, HSQOBJECT clsObj, const std::string& field) {
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, clsObj);
    sq_pushstring(vm, field.c_str(), static_cast<SQInteger>(field.size()));
    if (SQ_FAILED(sq_get(vm, -2))) {
        sq_settop(vm, top);
        return Result<Value>::failure(Diagnostic::error(
            DiagnosticCode::NotFound, "component field default is not readable", field, {}, "ecs.component"));
    }
    const SQInteger valueIndex = sq_gettop(vm);
    Result<Value>   value      = script::valueFromSquirrel(vm, valueIndex);
    sq_settop(vm, top);
    return value;
}

/**
 * @brief 已登记组件在注册表里的身份（名字 + 版本）。
 * @return 找到则为 true；类对象没登记过则为 false（该组件保持今天的槽位语义）。
 */
bool registeredIdentityOfClass(const ssq::Object& cls, ScriptComponentRecord* out) {
    const SQClass* klass = _class(cls.getRaw());
    if (klass == nullptr) return false;
    for (const auto& [key, entry] : currentScriptComponents()) {
        if (_class(entry.cls) != klass) continue;
        if (out != nullptr) {
            out->name       = key.first;
            out->version    = SchemaVersion(key.second);
            out->generation = entry.generation;
            out->retired    = false;
            out->fields     = entry.fields;
        }
        return true;
    }
    return false;
}

/**
 * @brief 为"已登记组件"在引擎侧建一份字段数据（默认值取自类声明）。
 *
 * 只对登记过的组件生效：没登记的组件保持纯槽位语义（最快，但原生侧读不到），这是
 * §9.62.1 按成本实测定下的策略，由脚本作者按组件选择。
 */
bool initializeScriptComponentValues(std::uint64_t entityId, ssq::Object cls) {
    ScriptComponentRecord identity;
    if (cls.getType() != ssq::Type::CLASS || !registeredIdentityOfClass(cls, &identity)) return false;

    Value::Object values;
    for (const std::string& field : identity.fields) {
        Result<Value> value = classFieldDefault(cls.getHandle(), cls.getRaw(), field);
        values.emplace(field, value.ok() ? std::move(value).takeValue() : Value::null());
    }
    const ScriptComponentStoreKey key{entityId, identity.name, identity.version};
    setScriptComponentValues(key, Value::object(std::move(values)))
        .ignore("a component whose values cannot be stored would silently lose its data if this succeeded");
    return true;
}

bool dropScriptComponentValues(std::uint64_t entityId) { return removeScriptComponentValues(entityId) > 0; }

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

// --- 性能缓存（万级实体稳态零分配） ---
// view 结果缓存：create/destroy 时按类链失效；C++ 桥接类（_cppHasView=true）不缓存。
eve._ecsViewCache <- {}       // EntityClass -> array
eve._ecsSlotsCache <- {}      // EntityClass -> { fieldName = ComponentClass }
eve._ecsFieldSlotsCache <- {} // EntityClass -> { fieldName = true }：只有这些槽位有真实字段
eve._ecsCompDefaults <- {}    // ComponentClass -> { fieldName = resolvedDefault }
eve._ecsRevision <- 0         // increments on script entity structural changes

function eve::_ecsResolveMarker(v) {
    if (v == this.Number || v == "number") return 0.0
    if (v == this.Boolean || v == "boolean") return false
    if (v == this.String || v == "string") return ""
    return v
}

function eve::_ecsIsComponentClass(obj) {
    if (typeof obj != "class") return false
    return this.isSubclass(obj, this.Component)
}

function eve::_ecsInstantiateComponent(compClass) {
    local c = compClass()
    if (compClass in this._ecsCompDefaults) {
        foreach (name, val in this._ecsCompDefaults[compClass]) c[name] = val
    } else {
        local fields = this.inspectClassFields(compClass)
        local resolved = {}
        foreach (name, val in fields) {
            local cur = null
            try { cur = c[name] } catch (e) { cur = null }
            if (cur == this.Number || cur == this.Boolean || cur == this.String
                || cur == "number" || cur == "boolean" || cur == "string") {
                c[name] = this._ecsResolveMarker(cur)
                resolved[name] <- c[name]
            }
        }
        this._ecsCompDefaults[compClass] <- resolved
    }
    return c
}

function eve::_ecsCollectSlots(cls) {
    if (cls in this._ecsSlotsCache) return this._ecsSlotsCache[cls]
    // { fieldName = ComponentClass } walking base classes (Entity → … → cls).
    local slots = {}
    local cur = cls
    local guard = 0
    while (cur != null && guard < 64) {
        if (!this.isSubclass(cur, this.Entity)) break

        local declared = null
        try { declared = cur.components } catch (e) { declared = null }
        if (typeof declared == "table") {
            foreach (k, v in declared) {
                if (!(k in slots) && this._ecsIsComponentClass(v))
                    slots[k] <- v
            }
        }

        local fields = this.inspectClassFields(cur)
        foreach (k, v in fields) {
            if (k == "components") continue
            if (!(k in slots) && this._ecsIsComponentClass(v))
                slots[k] <- v
        }

        cur = this.getClassBase(cur)
        guard += 1
    }
    this._ecsSlotsCache[cls] <- slots
    return slots
}

// 哪些槽位在实体类里有**真实字段**（`pos = Pos` 或 `pos = null`），哪些只是登记在
// `static components` 表里。判据是"这个类有没有声明同名的非函数成员"，而不是"它的值
// 是不是组件类"——`static components = { pos = Pos }` + `pos = null` 这种写法里字段值
// 是 null，但字段确实存在、可以赋值。
//
// 只有有字段的槽位能用 `e.pos` 访问：Squirrel 拒绝对 class 实例新建槽位，所以
// `e[name] = c` 对未声明的键会抛 "the index 'name' does not exist"（`=` 编译成
// _OP_SET；只有 `<-` 才是 new-slot 操作码，报的是另一句
// "class instances do not support the new slot operator"）。
function eve::_ecsCollectFieldSlots(cls) {
    if (cls in this._ecsFieldSlotsCache) return this._ecsFieldSlotsCache[cls]
    local fields = {}
    local cur = cls
    local guard = 0
    while (cur != null && guard < 64) {
        if (!this.isSubclass(cur, this.Entity)) break
        foreach (k, v in this.inspectClassFields(cur)) {
            if (k == "components") continue
            if (!(k in fields)) fields[k] <- true
        }
        cur = this.getClassBase(cur)
        guard += 1
    }
    this._ecsFieldSlotsCache[cls] <- fields
    return fields
}

function eve::_ecsInvalidateViews(cls) {
    local cur = cls
    local guard = 0
    while (cur != null && guard < 64) {
        if (cur in this._ecsViewCache) delete this._ecsViewCache[cur]
        if (!this.isSubclass(cur, this.Entity)) break
        cur = this.getClassBase(cur)
        guard += 1
    }
}

function eve::_ecsEnsureType(cls) {
    if (cls in this._ecsTypes) return this._ecsTypes[cls]
    local info = { cls = cls, instances = [] }
    this._ecsTypes[cls] <- info
    return info
}

function eve::_ecsRegisterInstance(entity, cls) {
    this._ecsEnsureType(cls).instances.push(entity)
    this._ecsRevision += 1
    this._ecsInvalidateViews(cls)
}

function eve::_ecsUnregisterInstance(entity, cls) {
    if (!(cls in this._ecsTypes)) return
    local arr = this._ecsTypes[cls].instances
    for (local i = arr.len() - 1; i >= 0; --i) {
        if (arr[i] == entity) {
            // O(1) swap-remove（顺序不保证）；视图缓存随后失效重建
            local last = arr.len() - 1
            if (i != last) arr[i] = arr[last]
            arr.pop()
            this._ecsRevision += 1
            break
        }
    }
    this._ecsInvalidateViews(cls)
}

function eve::_ecsCollectInstances(cls, out) {
    foreach (key, info in this._ecsTypes) {
        if (info.cls == cls || this.isSubclass(info.cls, cls)) {
            foreach (e in info.instances) {
                if (e != null && e.isAlive()) out.push(e)
            }
        }
    }
    // 稳定顺序：按实体 id 升序（= 创建顺序）。
    //
    // `_ecsUnregisterInstance` 用 swap-remove 保持 O(1) 销毁，所以 instances 数组本身的
    // 顺序在删除后会变——实测 [1,2,3,4] 删掉 2 得到 [1,4,3]。而 GPU System 按**视图顺序**
    // 把实体打包成 buffer 的行，回读时又按同一顺序写回（`unpackEcsFloats(ents, …)`），
    // `requestUploadRange/requestReadbackRange(first, count)` 更是直接按行号寻址：顺序一变，
    // "第 i 行是哪个实体"就变了，回读的值会落到另一个实体上。
    //
    // id 由 `eve.allocEntityId()` 单调分配且**不复用**，所以按 id 排序给出一个跨子类也
    // 成立的全序；`_ecsTypes` 的散列遍历顺序因此不再影响结果。排序只发生在缓存重建时
    // （结构性变更后第一次取视图），每帧取视图仍然是 O(n) 拷贝。
    out.sort(function(a, b) { return a._eid - b._eid })
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
        local fieldSlots = eve._ecsCollectFieldSlots(this)
        foreach (name, compClass in e._slots) {
            local c = eve._ecsInstantiateComponent(compClass)
            e._comps[name] <- c
            // 已登记组件（`eve.component`）在引擎侧另存一份字段数据，那是**权威**存储：原生侧
            // （打包 / 存档 / 编辑器）从它读。没登记的组件不建这份数据，保持纯槽位语义。
            eve._ecsInitComponentValues(e._eid, compClass)
            // 已声明的槽位（`pos = Pos`）同时写进实体字段，于是 `e.pos` 与
            // `e.getComponent(Pos)` 是**同一个对象**，两条路径读写一致（实测，见 §9.38）。
            // 只登记在 `static components` 表里、没有同名字段的槽位没有字段可写，它只
            // 能经 getComponent()/hasComponent() 访问。这里因此按"是否真实字段"分流，
            // 而不是用 try/catch 把真正的失败吞掉。
            if (name in fieldSlots) e[name] = c
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
        // 组件状态一并释放。留着它的话，销毁后的实体仍然可读可写、却对任何 view 无效
        // （静默 no-op），而且每个组件对象都被这个已销毁的实体继续引用着。
        // 两条引用都要清：`_comps` 是槽位表，已声明字段槽位是 `e.pos` 那条路。只清前者
        // 会让 `e.pos` 继续指向一个活着的组件实例，于是对已销毁实体写 `e.pos.x = 1`
        // 会静默改一个孤儿——正是上面要避免的状态。`this[name] = null` 对**已声明**成员
        // 是普通赋值，实测不抛（"会抛"的旧结论是把 `<-` 的行为记到了 `=` 上，见 §9.38）。
        if (_slots != null && _etype != null) {
            local fieldSlots = eve._ecsCollectFieldSlots(_etype)
            foreach (name, compClass in _slots) {
                if (name in fieldSlots) this[name] = null
            }
        }
        _comps = null
        // 引擎侧的组件数据也要清：它是权威存储，留着就是"销毁后仍可读"的孤儿数据。
        eve._ecsDropComponentValues(_eid)
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
    // uploadEachUpdate=false keeps the buffer GPU-resident after its initial upload.
    // readback=false leaves results resident until requestReadback() is called.
    function bindFields(binding, slot, fields, uploadEachUpdate = true, readback = null) {
        _bindings.push({ binding = binding, slot = slot, fields = fields,
                         uploadEachUpdate = uploadEachUpdate,
                         readback = readback, uploadedRevision = -1,
                         uploadRequested = false, readbackRequested = false,
                         uploadFirst = 0, uploadCount = 0,
                         readbackFirst = 0, readbackCount = 0,
                         source = null, sourceBinding = -1,
                         initializeFromCpu = true, schema = null })
        return this
    }

    // Typed binding schema: { slot, fields, scalar="f32", access="read_write" }.
    // write-only bindings allocate storage without an unnecessary CPU upload.
    function bindSchema(binding, schema, uploadEachUpdate = false, readback = false) {
        if (typeof schema != "table" || !("slot" in schema) || !("fields" in schema))
            throw "eve.ShaderSystem.bindSchema: schema requires slot and fields"
        if (typeof schema.slot != "string" || typeof schema.fields != "array" ||
            schema.fields.len() <= 0)
            throw "eve.ShaderSystem.bindSchema: invalid slot or fields"
        foreach (field in schema.fields)
            if (typeof field != "string")
                throw "eve.ShaderSystem.bindSchema: every field must be a string"
        local scalar = "scalar" in schema ? schema.scalar : "f32"
        if (scalar != "f32")
            throw "eve.ShaderSystem.bindSchema: only f32 is currently supported"
        local access = "access" in schema ? schema.access : "read_write"
        if (access != "read" && access != "write" && access != "read_write")
            throw "eve.ShaderSystem.bindSchema: access must be read, write, or read_write"
        local normalized = { slot = schema.slot, fields = schema.fields,
                             scalar = scalar, access = access,
                             strideFloats = schema.fields.len() }
        _bindings.push({ binding = binding, slot = schema.slot, fields = schema.fields,
                         uploadEachUpdate = access == "write" ? false : uploadEachUpdate,
                         readback = readback, uploadedRevision = -1,
                         uploadRequested = false, readbackRequested = false,
                         uploadFirst = 0, uploadCount = 0,
                         readbackFirst = 0, readbackCount = 0,
                         source = null, sourceBinding = -1,
                         initializeFromCpu = access != "write", schema = normalized })
        return this
    }

    function getBindingSchema(binding) {
        foreach (b in _bindings) if (b.binding == binding) return b.schema
        return null
    }

    // Generate backend declaration boilerplate; the kernel body remains explicit.
    function shaderDeclarations(language = "glsl") {
        local out = ""
        foreach (b in _bindings) {
            if (b.schema == null) continue
            if (language == "glsl") {
                local qualifier = b.schema.access == "read" ? "readonly " :
                                  b.schema.access == "write" ? "writeonly " : ""
                out += "layout(set = 0, binding = " + b.binding + ") " + qualifier +
                       "buffer EcsBinding" + b.binding + " { float data[]; } ecs" +
                       b.binding + ";\n"
            } else if (language == "wgsl") {
                local access = b.schema.access == "read" ? "read" : "read_write"
                out += "@group(0) @binding(" + b.binding + ") var<storage, " + access +
                       "> ecs" + b.binding + " : array<f32>;\n"
            } else {
                throw "eve.ShaderSystem.shaderDeclarations: language must be glsl or wgsl"
            }
        }
        return out
    }

    // Reuse another ShaderSystem's resident buffer without a CPU round trip.
    // The source system is retained and must be prepared before this consumer.
    function bindSharedFields(binding, sourceSystem, sourceBinding, slot, fields,
                              readback = false) {
        if (sourceSystem == null)
            throw "eve.ShaderSystem.bindSharedFields: source system is null"
        _bindings.push({ binding = binding, slot = slot, fields = fields,
                         uploadEachUpdate = false, readback = readback,
                         uploadedRevision = -1, uploadRequested = false,
                         readbackRequested = false, uploadFirst = 0, uploadCount = 0,
                         readbackFirst = 0, readbackCount = 0,
                         source = sourceSystem, sourceBinding = sourceBinding,
                         initializeFromCpu = false, schema = null })
        return this
    }

    function setBindingUpload(binding, enabled) {
        foreach (b in _bindings) if (b.binding == binding) b.uploadEachUpdate = enabled
        return this
    }

    function setBindingReadback(binding, enabled) {
        foreach (b in _bindings) if (b.binding == binding) b.readback = enabled
        return this
    }

    // Re-upload CPU component values on the next update. binding < 0 selects all.
    function requestUpload(binding = -1) {
        foreach (b in _bindings) {
            if ((binding < 0 || b.binding == binding) && b.source == null) {
                b.uploadRequested = true
                b.uploadFirst = 0
                b.uploadCount = -1
            }
        }
        return this
    }

    // Re-upload a stable contiguous entity range. Repeated requests are coalesced.
    function requestUploadRange(binding, firstEntity, entityCount) {
        if (firstEntity < 0 || entityCount <= 0)
            throw "eve.ShaderSystem.requestUploadRange: invalid entity range"
        foreach (b in _bindings) {
            if (b.binding != binding) continue
            if (b.source != null)
                throw "eve.ShaderSystem.requestUploadRange: shared binding is GPU-owned"
            if (!b.uploadRequested || b.uploadCount == 0) {
                b.uploadFirst = firstEntity
                b.uploadCount = entityCount
            } else if (b.uploadCount >= 0) {
                local oldEnd = b.uploadFirst + b.uploadCount
                local newEnd = firstEntity + entityCount
                local end = oldEnd > newEnd ? oldEnd : newEnd
                b.uploadFirst = b.uploadFirst < firstEntity ? b.uploadFirst : firstEntity
                b.uploadCount = end - b.uploadFirst
            }
            b.uploadRequested = true
        }
        return this
    }

    // Read GPU values back after the next update. binding < 0 selects all.
    function requestReadback(binding = -1) {
        foreach (b in _bindings) {
            if (binding < 0 || b.binding == binding) {
                b.readbackRequested = true
                b.readbackFirst = 0
                b.readbackCount = -1
            }
        }
        return this
    }

    // Read back a stable contiguous entity range. Repeated requests are coalesced.
    function requestReadbackRange(binding, firstEntity, entityCount) {
        if (firstEntity < 0 || entityCount <= 0)
            throw "eve.ShaderSystem.requestReadbackRange: invalid entity range"
        foreach (b in _bindings) {
            if (b.binding != binding) continue
            if (!b.readbackRequested || b.readbackCount == 0) {
                b.readbackFirst = firstEntity
                b.readbackCount = entityCount
            } else if (b.readbackCount >= 0) {
                local oldEnd = b.readbackFirst + b.readbackCount
                local newEnd = firstEntity + entityCount
                local end = oldEnd > newEnd ? oldEnd : newEnd
                b.readbackFirst = b.readbackFirst < firstEntity ? b.readbackFirst : firstEntity
                b.readbackCount = end - b.readbackFirst
            }
            b.readbackRequested = true
        }
        return this
    }

    function setFloat(index, value) {
        if (_backend != null) _backend.setFloat(index, value)
        return this
    }

    function getBackend() { return _backend }

    // Register renderer resources and submit a standard 80-byte GpuInstance binding
    // without copying the instance stream back through the CPU.
    function setGpuDrivenEnabled(enabled) {
        if (_gpu == null) throw "eve.ShaderSystem.setGpuDrivenEnabled: call setGpgpu first"
        _gpu.setGpuDrivenEnabled(enabled)
        return this
    }

    function gpuDrivenMeshRecord(mesh) {
        if (_gpu == null) throw "eve.ShaderSystem.gpuDrivenMeshRecord: call setGpgpu first"
        return _gpu.gpuDrivenMeshRecord(mesh)
    }

    function gpuDrivenMaterialRecord(material) {
        if (_gpu == null)
            throw "eve.ShaderSystem.gpuDrivenMaterialRecord: call setGpgpu first"
        return _gpu.gpuDrivenMaterialRecord(material)
    }

    function submitResidentInstances(binding, buckets, instanceCount = -1,
                                     offsetBytes = 0) {
        if (_gpu == null || _backend == null)
            throw "eve.ShaderSystem.submitResidentInstances: call setGpgpu first"
        local buffer = _backend.getBuffer(binding)
        if (buffer == null)
            throw "eve.ShaderSystem.submitResidentInstances: binding has no buffer"
        if (instanceCount < 0) instanceCount = entities().len()
        return _gpu.submitResidentInstances(buffer, buckets, instanceCount, offsetBytes)
    }

    function _prepareGpuBatch() {
        if (_backend == null || _gpu == null) return
        if (!("packEcsFloats" in eve) || !("unpackEcsFloats" in eve)) return

        local ents = entities()
        local n = ents.len()
        if (n <= 0) return null

        foreach (b in _bindings) {
            local floatsPer = b.fields.len()
            if (floatsPer <= 0) continue
            if (b.source != null) {
                local sourceBackend = b.source.getBackend()
                local sourceBuffer = sourceBackend == null ? null :
                                     sourceBackend.getBuffer(b.sourceBinding)
                if (sourceBuffer == null)
                    throw "eve.ShaderSystem shared source must be prepared before consumer"
                if (sourceBuffer.getSize() < n * floatsPer * 4)
                    throw "eve.ShaderSystem shared buffer is smaller than consumer view"
                _backend.attachBuffer(b.binding, sourceBuffer)
                continue
            }
            local buf = _backend.ensureBuffer(b.binding, n * floatsPer)
            local structureChanged = b.uploadedRevision != eve._ecsRevision
            local fullUpload = (structureChanged && (b.initializeFromCpu || b.uploadRequested)) ||
                               b.uploadEachUpdate ||
                               (b.uploadRequested && b.uploadCount < 0)
            if (fullUpload) {
                eve.packEcsFloats(ents, b.slot, b.fields, buf)
                b.uploadedRevision = eve._ecsRevision
                b.uploadRequested = false
                b.uploadCount = 0
            } else if (structureChanged) {
                // Output-only schema: capacity/order changed, but the shader is
                // authoritative and promises to initialize every accessed record.
                b.uploadedRevision = eve._ecsRevision
            } else if (b.uploadRequested) {
                if (!("packEcsFloatsRange" in eve))
                    throw "eve.ShaderSystem.requestUploadRange requires gpgpu range packing"
                eve.packEcsFloatsRange(ents, b.slot, b.fields, buf,
                                       b.uploadFirst, b.uploadCount)
                b.uploadRequested = false
                b.uploadCount = 0
            }
        }
        return { entities = ents, count = n }
    }

    function _completeGpuBatch(batch) {
        if (batch == null) return
        local ents = batch.entities
        local n = batch.count
        foreach (b in _bindings) {
            local shouldReadback = b.readback == null ? _readback : b.readback
            if (shouldReadback || (b.readbackRequested && b.readbackCount < 0)) {
                local floatsPer = b.fields.len()
                if (floatsPer <= 0) continue
                local buf = _backend.getBuffer(b.binding)
                eve.unpackEcsFloats(ents, b.slot, b.fields, buf, n)
                b.readbackRequested = false
                b.readbackCount = 0
            } else if (b.readbackRequested) {
                if (!("unpackEcsFloatsRange" in eve))
                    throw "eve.ShaderSystem.requestReadbackRange requires gpgpu range unpacking"
                local buf = _backend.getBuffer(b.binding)
                eve.unpackEcsFloatsRange(ents, b.slot, b.fields, buf,
                                         b.readbackFirst, b.readbackCount)
                b.readbackRequested = false
                b.readbackCount = 0
            }
        }
    }

    // Record into a shared GpuSequence. Call completeRecorded(batch) after submit().
    function record(sequence, dt) {
        if (sequence == null) throw "eve.ShaderSystem.record: sequence is null"
        local batch = _prepareGpuBatch()
        if (batch != null) _backend.recordDispatch(sequence, batch.count, dt)
        return batch
    }

    function completeRecorded(batch) {
        _completeGpuBatch(batch)
        return this
    }

    function update(dt) {
        local batch = _prepareGpuBatch()
        if (batch == null) return
        _backend.dispatch(batch.count, dt)
        _completeGpuBatch(batch)
    }
}

function eve::view(entityClass) {
    if (entityClass == null) return []
    // C++ 桥接类的实体在脚本外增删（无法通知脚本），不缓存，每次新鲜收集
    local cppBacked = false
    if ("_cppHasView" in this) cppBacked = this._cppHasView(entityClass)

    // 每次调用都返回**新数组**：缓存只保存脚本侧收集结果（遍历 _ecsTypes 才是贵的
    // 那一步），绝不外借。否则调用方 sort/pop/append 自己的视图就会污染其他使用者。
    local out = []
    if (cppBacked) {
        this._ecsCollectInstances(entityClass, out)
    } else {
        if (!(entityClass in this._ecsViewCache)) {
            local collected = []
            this._ecsCollectInstances(entityClass, collected)
            this._ecsViewCache[entityClass] <- collected
        }
        local cached = this._ecsViewCache[entityClass]
        local count = cached.len()
        out.resize(count)
        for (local i = 0; i < count; ++i) out[i] = cached[i]
    }
    if ("_cppCollect" in this) this._cppCollect(entityClass, out)
    // No sort here. Entities appended by a registered C++ collector
    // (registerCppEntityView) are not `eve.Entity` instances and have no `_eid`; sorting them
    // threw "the index '_eid' does not exist" (caught by ECS.script.cppCreatedEntitiesVisible).
    // The collector owns their order; the id-ordered contract covers script instances, which
    // `_ecsCollectInstances` already sorted.
    return out
}

function eve::ecsReady() {
    // This function is installed only after the complete ECS script has run.
    // Avoid probing the delegated eve table here: Squirrel's `in` lookup may
    // invoke its lazy _get handler and turn a readiness query into an error.
    return true
}
)SQ";

void injectEcsScript(ssq::Table& eveTable) {
    HSQUIRRELVM vm = eveTable.getHandle();
    const SQInteger top = sq_gettop(vm);
    if (SQ_FAILED(sq_compilebuffer(vm, kEcsScript,
                                   static_cast<SQInteger>(std::strlen(kEcsScript)),
                                   "ecs.nut", SQTrue))) {
        sq_settop(vm, top);
        const script::ScriptErrorContext ctx = script::captureCompileError(vm);
        std::fprintf(stderr, "EVEngine: ECS bootstrap script failed to compile: %s\n",
                     ctx.empty() ? "unknown error" : ctx.message.c_str());
        return;
    }
    sq_pushroottable(vm);
    if (SQ_FAILED(sq_call(vm, 1, SQFalse, SQTrue))) {
        const script::ScriptErrorContext ctx = script::takeLastScriptError(vm);
        std::fprintf(stderr, "EVEngine: ECS bootstrap script failed to run: %s\n",
                     ctx.empty() ? "unknown error"
                                 : script::formatScriptError(ctx).c_str());
    }
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
        auto it = cppEntityViews().find(cur._unVal.pClass);
        if (it != cppEntityViews().end()) {
            sq_settop(vm, top);
            it->second(out);
            return;
        }
        if (SQ_FAILED(sq_getbase(vm, -1)))
            break;
        sq_remove(vm, -2);  // drop current class, keep base
    }
    sq_settop(vm, top);
}

/**
 * Script hook for view-cache policy: returns true when any class on the chain
 * (cls → base → …) is registered via registerCppEntityView(). C++ entities can
 * appear/disappear outside script knowledge, so such views must not be cached.
 */
bool cppHasView(ssq::Object cls) {
    if (cls.getType() != ssq::Type::CLASS) return false;
    HSQUIRRELVM vm = cls.getHandle();
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, cls.getRaw());
    for (int guard = 0; guard < 64; ++guard) {
        if (sq_gettype(vm, -1) != OT_CLASS) break;
        HSQOBJECT cur;
        sq_getstackobj(vm, -1, &cur);
        if (cppEntityViews().find(cur._unVal.pClass) != cppEntityViews().end()) {
            sq_settop(vm, top);
            return true;
        }
        if (SQ_FAILED(sq_getbase(vm, -1))) break;
        sq_remove(vm, -2);  // drop current class, keep base
    }
    sq_settop(vm, top);
    return false;
}

}  // namespace

Result<void> registerScriptComponent(const std::string& name, SchemaVersion version, const ssq::Object& cls) {
    if (name.empty())
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "script component name must not be empty", {}, {}, "ecs.component"));
    if (version.isZero())
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "script component version must be a positive integer", name, {},
                                                       "ecs.component"));
    if (cls.getType() != ssq::Type::CLASS)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvalidArgument,
                                                       "eve.component needs a class, not a value or instance", name, {},
                                                       "ecs.component"));

    HSQUIRRELVM vm = cls.getHandle();
    HSQOBJECT   o  = cls.getRaw();
    sq_addref(vm, &o);

    // 登记时读出这一代声明的字段（按声明顺序）：它既是组件的 schema，也是第 2 步"取走字段槽位、
    // 让引擎成为权威存储"要用的清单——必须先读出来，才能安全地摘掉。
    const std::vector<std::string> fields = declaredInstanceFields(o);

    auto&      current = currentScriptComponents();
    const auto key     = ScriptComponentKey{name, version.value()};
    auto       found   = current.find(key);
    if (found == current.end()) {
        current.emplace(key, ScriptComponentEntry(vm, o, Generation(1), fields));
        return Result<void>::success(Status::success(StatusCode::Applied));
    }

    // Same name and version again: hot reload redefined the class. Record the retired
    // generation (metadata only) and advance the counter, so a caller can tell that the
    // identity it holds refers to an older generation of the same component. The retired
    // row keeps the *previous* field list: migration needs to know which fields it moves.
    const Generation previous = found->second.generation;
    retiredScriptComponents().push_back(ScriptComponentRecord{name, version, previous, true, found->second.fields});
    const auto next = previous.incremented();
    if (!next)
        return Result<void>::failure(Diagnostic::error(DiagnosticCode::InvariantViolation,
                                                       "script component generation counter is exhausted", name, {},
                                                       "ecs.component"));
    // Only release a reference belonging to *this* VM: a stale entry from another VM must
    // not be released, because that VM may already be destroyed and sq_release() would
    // dereference its freed RefTable (measured access violation, plan doc §9.49).
    if (found->second.vm == vm) sq_release(found->second.vm, &found->second.cls);
    found->second = ScriptComponentEntry(vm, o, *next, fields);
    return Result<void>::success(Status::success(StatusCode::Applied));
}

const ScriptComponentRecord* findScriptComponent(const std::string& name, SchemaVersion version) {
    static ScriptComponentRecord scratch;
    const auto&                  current = currentScriptComponents();
    const auto                   found   = current.find(ScriptComponentKey{name, version.value()});
    if (found == current.end()) return nullptr;
    scratch = ScriptComponentRecord{found->first.first, version, found->second.generation, false, found->second.fields};
    return &scratch;
}

std::vector<ScriptComponentRecord> scriptComponentRecords() {
    std::vector<ScriptComponentRecord> out;
    out.reserve(currentScriptComponents().size() + retiredScriptComponents().size());
    for (const auto& [key, entry] : currentScriptComponents())
        out.push_back(
            ScriptComponentRecord{key.first, SchemaVersion(key.second), entry.generation, false, entry.fields});
    for (const ScriptComponentRecord& retired : retiredScriptComponents()) out.push_back(retired);
    std::sort(out.begin(), out.end(), [](const ScriptComponentRecord& a, const ScriptComponentRecord& b) {
        if (a.name != b.name) return a.name < b.name;
        if (a.version != b.version) return a.version < b.version;
        return a.generation < b.generation;
    });
    return out;
}

void exposeECS(ssq::Table& table) {
    table.set("Number", std::string("number"));
    table.set("Boolean", std::string("boolean"));
    table.set("String", std::string("string"));
    // ecsReady / view / Entity / Component are provided by the injected script.

    table.addFunc("inspectClassFields", inspectClassFields);
    table.addFunc("isSubclass", scriptIsSubclass);
    table.addFunc("getClassBase", getClassBase);
    table.addFunc("allocEntityId", allocEntityId);
    table.addFunc("_cppHasView", cppHasView);
    // `eve.component(name, version, cls)` registers the component's *identity*. It is
    // optional today (classes keep working unregistered), but it is what lets a hot
    // reload match a redefined class to the instances created by the previous one.
    table.addFunc("component", [vm = table.getHandle()](std::string name, int version, ssq::Object cls) {
        return script::projectResult(
            vm, registerScriptComponent(name, SchemaVersion(static_cast<std::uint64_t>(version)), cls));
    });
    table.addFunc("_cppCollect", std::function<void(ssq::Class, ssq::Array)>(cppCollect));
    // 已登记组件在引擎侧的数据：创建实体时按 schema 默认值建一份，销毁实体时一并清掉。
    // 返回 false 表示"这个组件没登记过"，脚本侧据此什么都不做（保持纯槽位语义）。
    table.addFunc("_ecsInitComponentValues", [](std::uint64_t entityId, ssq::Object cls) {
        return initializeScriptComponentValues(entityId, cls);
    });
    table.addFunc("_ecsDropComponentValues",
                  [](std::uint64_t entityId) { return dropScriptComponentValues(entityId); });

    injectEcsScript(table);

    ecsScriptInjected() = true;
    // A new VM re-runs every hook (SceneEntity class, …) against this table.
    postEcsHooksFlushed() = 0;
    flushPostEcsHooks(table);
}

void prepareEcsScriptInjection() {
    // Class identities belong to one VM. Clear them when a new eve table is
    // prepared, before any lazy/native exposer registers the next VM's classes.
    cppEntityViews().clear();
    // Script component registrations (eve.component) hold a Squirrel reference
    // into the VM that made them. By the time the next table is prepared that VM
    // is already destroyed, so the entries must be dropped **without** releasing:
    // sq_release() on a destroyed VM is a use-after-free (see the plan doc §9.49).
    // The retired-generation history is per VM too: generations count reloads of one
    // VM's script, so a new VM starts with no identity history.
    currentScriptComponents().clear();
    retiredScriptComponents().clear();
    // 引擎侧组件数据同样属于"一个 VM 的脚本世界"：新 VM 从空开始。
    clearScriptComponentStore();
    ecsScriptInjected()  = false;
    postEcsHooksFlushed() = 0;
}

void registerCppEntityView(const ssq::Class& cls, CppEntityViewFn fn) {
    cppEntityViews()[cls.getRaw()._unVal.pClass] = std::move(fn);
}

void registerPostEcsHook(PostEcsHook fn) {
    postEcsHooks().push_back(std::move(fn));
}

void flushPostEcsHooks(ssq::Table& table) {
    if (!ecsScriptInjected()) return;
    auto& hooks = postEcsHooks();
    auto& n     = postEcsHooksFlushed();
    while (n < hooks.size()) {
        try {
            hooks[n](table);
        } catch (...) {
            // A failing module hook must not break VM exposure.
        }
        ++n;
    }
}

void exposeECSToVM(ssq::VM& vm) {
    try {
        ssq::Table eveTable(vm.find("eve"));
        exposeECS(eveTable);
    } catch (...) {
    }
}

}  // namespace eve
