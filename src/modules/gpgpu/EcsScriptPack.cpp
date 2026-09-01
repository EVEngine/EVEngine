#include "gpgpu/EcsScriptPack.h"
#include "gpgpu/GpuBuffer.h"

#include "common/Exception.h"

#include <simplesquirrel/simplesquirrel.hpp>
#include <squirrel.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace eve::gpgpu {
namespace {

bool readStringField(HSQUIRRELVM vm, SQInteger idx, std::string &out) {
    if (sq_gettype(vm, idx) != OT_STRING) return false;
    const SQChar *s = nullptr;
    if (SQ_FAILED(sq_getstring(vm, idx, &s)) || !s) return false;
    out = s;
    return true;
}

bool readFloatAt(HSQUIRRELVM vm, SQInteger idx, float &out) {
    const SQObjectType t = sq_gettype(vm, idx);
    if (t == OT_FLOAT) {
        SQFloat v = 0;
        sq_getfloat(vm, idx, &v);
        out = float(v);
        return true;
    }
    if (t == OT_INTEGER) {
        SQInteger v = 0;
        sq_getinteger(vm, idx, &v);
        out = float(v);
        return true;
    }
    return false;
}

bool getInstanceSlot(HSQUIRRELVM vm, HSQOBJECT instance, const std::string &name,
                     HSQOBJECT &out) {
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, instance);
    sq_pushstring(vm, name.c_str(), -1);
    if (SQ_FAILED(sq_get(vm, -2))) {
        sq_settop(vm, top);
        return false;
    }
    sq_getstackobj(vm, -1, &out);
    sq_addref(vm, &out);
    sq_settop(vm, top);
    return true;
}

bool getNumberField(HSQUIRRELVM vm, HSQOBJECT instance, const std::string &name, float &out) {
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, instance);
    sq_pushstring(vm, name.c_str(), -1);
    if (SQ_FAILED(sq_get(vm, -2))) {
        sq_settop(vm, top);
        return false;
    }
    const bool ok = readFloatAt(vm, -1, out);
    sq_settop(vm, top);
    return ok;
}

bool setNumberField(HSQUIRRELVM vm, HSQOBJECT instance, const std::string &name, float value) {
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, instance);
    sq_pushstring(vm, name.c_str(), -1);
    sq_pushfloat(vm, SQFloat(value));
    // Prefer set (existing slot). Fall back to newslot for unusual components.
    if (SQ_FAILED(sq_set(vm, -3))) {
        sq_settop(vm, top);
        sq_pushobject(vm, instance);
        sq_pushstring(vm, name.c_str(), -1);
        sq_pushfloat(vm, SQFloat(value));
        if (SQ_FAILED(sq_newslot(vm, -3, SQFalse))) {
            sq_settop(vm, top);
            return false;
        }
    }
    sq_settop(vm, top);
    return true;
}

std::vector<std::string> collectFieldNames(ssq::Object fieldsObj) {
    std::vector<std::string> names;
    HSQUIRRELVM vm = fieldsObj.getHandle();
    if (!vm) return names;
    HSQOBJECT arr = fieldsObj.getRaw();
    if (arr._type != OT_ARRAY) return names;

    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, arr);
    const SQInteger len = sq_getsize(vm, -1);
    names.reserve(size_t(std::max<SQInteger>(0, len)));
    for (SQInteger i = 0; i < len; ++i) {
        sq_pushinteger(vm, i);
        if (SQ_FAILED(sq_get(vm, -2))) continue;
        std::string name;
        if (readStringField(vm, -1, name)) names.push_back(name);
        sq_pop(vm, 1);
    }
    sq_settop(vm, top);
    return names;
}

int entityArrayLen(HSQUIRRELVM vm, HSQOBJECT arr) {
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, arr);
    const SQInteger len = sq_getsize(vm, -1);
    sq_settop(vm, top);
    return int(std::max<SQInteger>(0, len));
}

}  // namespace

int packScriptEntityFloats(ssq::Object entitiesObj, const std::string &slot,
                           ssq::Object fieldsObj, GpuBuffer *buf) {
    return packScriptEntityFloatsRange(entitiesObj, slot, fieldsObj, buf, 0, std::numeric_limits<int>::max());
}

int packScriptEntityFloatsRange(ssq::Object entitiesObj, const std::string &slot, ssq::Object fieldsObj, GpuBuffer *buf,
                                int firstEntity, int entityCount) {
    if (!buf) throw Exception("packScriptEntityFloats: buffer is null");
    if (slot.empty()) throw Exception("packScriptEntityFloats: empty slot");

    HSQUIRRELVM vm = entitiesObj.getHandle();
    if (!vm) return 0;
    HSQOBJECT ents = entitiesObj.getRaw();
    if (ents._type != OT_ARRAY) return 0;

    const std::vector<std::string> fields = collectFieldNames(fieldsObj);
    if (fields.empty()) return 0;

    const int total = entityArrayLen(vm, ents);
    const int first = std::clamp(firstEntity, 0, total);
    const int n     = std::clamp(entityCount, 0, total - first);
    if (n <= 0) return 0;

    const int floatsPer = int(fields.size());
    std::vector<float> tmp(size_t(n) * size_t(floatsPer), 0.f);

    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, ents);
    for (int i = 0; i < n; ++i) {
        sq_pushinteger(vm, first + i);
        if (SQ_FAILED(sq_get(vm, -2))) {
            sq_settop(vm, top);
            throw Exception("packScriptEntityFloats: bad entity index %d", first + i);
        }
        if (sq_gettype(vm, -1) != OT_INSTANCE) {
            sq_settop(vm, top);
            throw Exception("packScriptEntityFloats: entity[%d] is not an instance", first + i);
        }
        HSQOBJECT entity{};
        sq_getstackobj(vm, -1, &entity);

        HSQOBJECT comp{};
        sq_resetobject(&comp);
        if (!getInstanceSlot(vm, entity, slot, comp) || comp._type != OT_INSTANCE) {
            if (comp._type != OT_NULL) sq_release(vm, &comp);
            sq_settop(vm, top);
            throw Exception("packScriptEntityFloats: missing component slot '%s'", slot.c_str());
        }

        for (int f = 0; f < floatsPer; ++f) {
            float v = 0.f;
            if (!getNumberField(vm, comp, fields[size_t(f)], v)) {
                sq_release(vm, &comp);
                sq_settop(vm, top);
                throw Exception("packScriptEntityFloats: missing field '%s.%s'", slot.c_str(),
                                fields[size_t(f)].c_str());
            }
            tmp[size_t(i) * size_t(floatsPer) + size_t(f)] = v;
        }
        sq_release(vm, &comp);
        sq_pop(vm, 1);  // entity
    }
    sq_settop(vm, top);

    const int needBytes = (first + n) * floatsPer * int(sizeof(float));
    if (buf->getSize() < needBytes)
        throw Exception("packScriptEntityFloats: buffer too small (%d < %d)", buf->getSize(),
                        needBytes);
    buf->writeFloat32s(tmp.data(), n * floatsPer, first * floatsPer);
    return n;
}

int unpackScriptEntityFloats(ssq::Object entitiesObj, const std::string &slot,
                             ssq::Object fieldsObj, GpuBuffer *buf, int entityCount) {
    return unpackScriptEntityFloatsRange(entitiesObj, slot, fieldsObj, buf, 0, entityCount);
}

int unpackScriptEntityFloatsRange(ssq::Object entitiesObj, const std::string &slot, ssq::Object fieldsObj,
                                  GpuBuffer *buf, int firstEntity, int entityCount) {
    if (!buf) throw Exception("unpackScriptEntityFloats: buffer is null");
    if (slot.empty()) throw Exception("unpackScriptEntityFloats: empty slot");
    if (entityCount <= 0) return 0;

    HSQUIRRELVM vm = entitiesObj.getHandle();
    if (!vm) return 0;
    HSQOBJECT ents = entitiesObj.getRaw();
    if (ents._type != OT_ARRAY) return 0;

    const std::vector<std::string> fields = collectFieldNames(fieldsObj);
    if (fields.empty()) return 0;

    const int nArr = entityArrayLen(vm, ents);
    const int first = std::clamp(firstEntity, 0, nArr);
    const int n     = std::clamp(entityCount, 0, nArr - first);
    if (n <= 0) return 0;

    const int floatsPer = int(fields.size());
    std::vector<float> tmp(size_t(n) * size_t(floatsPer), 0.f);
    const int          needBytes = (first + n) * floatsPer * int(sizeof(float));
    if (buf->getSize() < needBytes)
        throw Exception("unpackScriptEntityFloats: buffer too small (%d < %d)", buf->getSize(), needBytes);
    buf->readFloat32s(tmp.data(), n * floatsPer, first * floatsPer);

    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, ents);
    for (int i = 0; i < n; ++i) {
        sq_pushinteger(vm, first + i);
        if (SQ_FAILED(sq_get(vm, -2))) {
            sq_settop(vm, top);
            throw Exception("unpackScriptEntityFloats: bad entity index %d", first + i);
        }
        HSQOBJECT entity{};
        sq_getstackobj(vm, -1, &entity);

        HSQOBJECT comp{};
        sq_resetobject(&comp);
        if (!getInstanceSlot(vm, entity, slot, comp) || comp._type != OT_INSTANCE) {
            if (comp._type != OT_NULL) sq_release(vm, &comp);
            sq_settop(vm, top);
            throw Exception("unpackScriptEntityFloats: missing component slot '%s'", slot.c_str());
        }

        for (int f = 0; f < floatsPer; ++f) {
            const float v = tmp[size_t(i) * size_t(floatsPer) + size_t(f)];
            if (!setNumberField(vm, comp, fields[size_t(f)], v)) {
                sq_release(vm, &comp);
                sq_settop(vm, top);
                throw Exception("unpackScriptEntityFloats: cannot set '%s.%s'", slot.c_str(),
                                fields[size_t(f)].c_str());
            }
        }
        sq_release(vm, &comp);
        sq_pop(vm, 1);
    }
    sq_settop(vm, top);
    return n;
}

}  // namespace eve::gpgpu
