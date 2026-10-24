#include "animation/ControlRigInternal.h"
#include "common/Json.h"
#include "common/Value.h"

namespace eve::animation {
namespace {
using namespace rig_detail;
bool fields(const json::Value& v, std::initializer_list<std::string_view> allowed) {
    if (!v.isObject() || v.size() != allowed.size()) return false;
    for (auto name : allowed)
        if (!v.get(std::string(name).c_str())) return false;
    return true;
}
bool number(const json::Value& v, double& out) {
    if (!v.isNumber()) return false;
    out = v.asDouble();
    return std::isfinite(out);
}
Value transformValue(const TransformTRS& t) {
    return Value::array({t.px, t.py, t.pz, t.qx, t.qy, t.qz, t.qw, t.sx, t.sy, t.sz});
}
bool transform(const json::Value& v, TransformTRS& t) {
    if (!v.isArray() || v.size() != 10) return false;
    float* destinations[]{&t.px, &t.py, &t.pz, &t.qx, &t.qy, &t.qz, &t.qw, &t.sx, &t.sy, &t.sz};
    for (size_t i = 0; i < 10; ++i) {
        double n = 0;
        if (!number(v.at(i), n)) return false;
        *destinations[i] = static_cast<float>(n);
    }
    return validTRS(t);
}
bool string(const json::Value& object, std::string_view name, std::string& out) {
    const auto v = object.get(std::string(name).c_str());
    if (!v || !v.isString()) return false;
    out = v.asString();
    return true;
}
Value axisValue(const RigAxisLimit& a) {
    return Value::object({{"mode", a.mode == RigAxisMode::Free     ? "free"
                                   : a.mode == RigAxisMode::Locked ? "locked"
                                                                   : "limited"},
                          {"min", a.minimum},
                          {"max", a.maximum}});
}
bool axis(const json::Value& v, RigAxisLimit& a) {
    std::string mode;
    double      lo = 0, hi = 0;
    if (!fields(v, {"mode", "min", "max"}) || !string(v, "mode", mode) || !number(v.get("min"), lo) ||
        !number(v.get("max"), hi))
        return false;
    if (mode == "free")
        a.mode = RigAxisMode::Free;
    else if (mode == "locked")
        a.mode = RigAxisMode::Locked;
    else if (mode == "limited")
        a.mode = RigAxisMode::Limited;
    else
        return false;
    a.minimum = static_cast<float>(lo);
    a.maximum = static_cast<float>(hi);
    return true;
}
Value limitsValue(const RigControlLimits& l) {
    Value translation = Value::array({}), rotation = Value::array({});
    for (size_t i = 0; i < 3; ++i) {
        translation.pushBack(axisValue(l.translation[i]));
        rotation.pushBack(axisValue(l.rotation[i]));
    }
    return Value::object({{"reference", transformValue(l.reference)},
                          {"translation", std::move(translation)},
                          {"rotation", std::move(rotation)},
                          {"scale", axisValue(l.scale)},
                          {"extension", l.extension},
                          {"minLength", l.minimumLength},
                          {"maxLength", l.maximumLength}});
}
bool limits(const json::Value& v, RigControlLimits& l) {
    double lo = 0, hi = 0;
    if (!fields(v, {"reference", "translation", "rotation", "scale", "extension", "minLength", "maxLength"}) ||
        !transform(v.get("reference"), l.reference) || !v.get("translation").isArray() ||
        v.get("translation").size() != 3 || !v.get("rotation").isArray() || v.get("rotation").size() != 3 ||
        !axis(v.get("scale"), l.scale) || !v.get("extension").isBool() || !number(v.get("minLength"), lo) ||
        !number(v.get("maxLength"), hi))
        return false;
    for (size_t i = 0; i < 3; ++i)
        if (!axis(v.get("translation").at(i), l.translation[i]) || !axis(v.get("rotation").at(i), l.rotation[i]))
            return false;
    l.extension     = v.get("extension").asBool();
    l.minimumLength = static_cast<float>(lo);
    l.maximumLength = static_cast<float>(hi);
    return true;
}
}  // namespace

Result<Value> encodeControlRig(const RigDefinition& d) {
    auto valid = validateRigDefinition(d);
    if (!valid.ok()) return Result<Value>::failure(valid.status());
    Value controls = Value::array({}), nodes = Value::array({});
    for (const auto& c : d.controls) {
        Value keys = Value::array({});
        for (const auto& k : c.keys) {
            auto projected = constrainRigControl(c.limits, k.transform);
            if (!projected.ok()) return Result<Value>::failure(projected.status());
            keys.pushBack(Value::object({{"time", k.time}, {"transform", transformValue(projected.value())}}));
        }
        auto projected = constrainRigControl(c.limits, c.initial);
        if (!projected.ok()) return Result<Value>::failure(projected.status());
        controls.pushBack(Value::object({{"id", c.id},
                                         {"parent", c.parent},
                                         {"initial", transformValue(projected.value())},
                                         {"limits", limitsValue(c.limits)},
                                         {"shapeSize", c.shapeSize},
                                         {"keys", std::move(keys)}}));
    }
    for (const auto& n : d.nodes) {
        Value after = Value::array({});
        for (const auto& id : n.after) after.pushBack(id);
        const std::string kind = n.operation == RigOperation::FK    ? "fk"
                                 : n.operation == RigOperation::Aim ? "aim"
                                                                    : "twoBoneIK";
        nodes.pushBack(Value::object({{"id", n.id},
                                      {"control", n.control},
                                      {"bone", n.bone},
                                      {"middle", n.middle},
                                      {"tip", n.tip},
                                      {"operation", kind},
                                      {"weight", n.weight},
                                      {"after", std::move(after)}}));
    }
    return Result<Value>::success(Value::object({{"schema", "eve.control-rig"},
                                                 {"version", std::int64_t{2}},
                                                 {"duration", d.duration},
                                                 {"controls", std::move(controls)},
                                                 {"nodes", std::move(nodes)}}));
}

Result<RigDefinition> decodeControlRig(const Value& value) {
    auto text = value.toJson();
    if (!text.ok()) return Result<RigDefinition>::failure(text.status());
    auto parsed = json::Document::parse(text.value());
    if (!parsed.valid()) return error<RigDefinition>("invalid rig JSON representation");
    const auto v = parsed.root();
    if (!fields(v, {"schema", "version", "duration", "controls", "nodes"}))
        return error<RigDefinition>("unexpected or missing document fields");
    if (!v.get("schema").isString() || v.get("schema").asString() != "eve.control-rig" || !v.get("version").isInt64() ||
        (v.get("version").asInt() != 1 && v.get("version").asInt() != 2))
        return error<RigDefinition>("unsupported schema/version");
    RigDefinition d;
    if (!number(v.get("duration"), d.duration) || !v.get("controls").isArray() || !v.get("nodes").isArray())
        return error<RigDefinition>("invalid document data");
    if (v.get("controls").size() > 4096 || v.get("nodes").size() > 4096)
        return error<RigDefinition>("rig capacity exceeded");
    for (size_t ci = 0; ci < v.get("controls").size(); ++ci) {
        const auto item = v.get("controls").at(ci);
        RigControl c;
        double     size   = 0;
        const bool legacy = v.get("version").asInt() == 1;
        if (!(legacy ? fields(item, {"id", "parent", "initial", "shapeSize", "keys"})
                     : fields(item, {"id", "parent", "initial", "shapeSize", "keys", "limits"})) ||
            !string(item, "id", c.id) || !string(item, "parent", c.parent) ||
            !transform(item.get("initial"), c.initial) || !number(item.get("shapeSize"), size) ||
            !item.get("keys").isArray())
            return error<RigDefinition>("invalid control fields");
        c.shapeSize        = static_cast<float>(size);
        c.limits.reference = c.initial;
        if (!legacy && !limits(item.get("limits"), c.limits)) return error<RigDefinition>("invalid limits fields");
        if (item.get("keys").size() > 100000) return error<RigDefinition>("key capacity exceeded");
        for (size_t ki = 0; ki < item.get("keys").size(); ++ki) {
            const auto key = item.get("keys").at(ki);
            RigKey     k;
            if (!fields(key, {"time", "transform"}) || !number(key.get("time"), k.time) ||
                !transform(key.get("transform"), k.transform))
                return error<RigDefinition>("invalid key fields");
            c.keys.push_back(k);
        }
        d.controls.push_back(std::move(c));
    }
    for (size_t ni = 0; ni < v.get("nodes").size(); ++ni) {
        const auto  item = v.get("nodes").at(ni);
        RigNode     n;
        std::string kind;
        double      weight = 0;
        if (!fields(item, {"id", "control", "bone", "middle", "tip", "operation", "weight", "after"}) ||
            !string(item, "id", n.id) || !string(item, "control", n.control) || !string(item, "bone", n.bone) ||
            !string(item, "middle", n.middle) || !string(item, "tip", n.tip) || !string(item, "operation", kind) ||
            !number(item.get("weight"), weight) || !item.get("after").isArray())
            return error<RigDefinition>("invalid node fields");
        if (kind == "fk")
            n.operation = RigOperation::FK;
        else if (kind == "aim")
            n.operation = RigOperation::Aim;
        else if (kind == "twoBoneIK")
            n.operation = RigOperation::TwoBoneIK;
        else
            return error<RigDefinition>("unknown operation");
        n.weight = static_cast<float>(weight);
        if (item.get("after").size() > 4096) return error<RigDefinition>("dependency capacity exceeded");
        for (size_t ai = 0; ai < item.get("after").size(); ++ai) {
            const auto dependency = item.get("after").at(ai);
            if (!dependency.isString()) return error<RigDefinition>("dependency must be a node ID");
            n.after.push_back(dependency.asString());
        }
        d.nodes.push_back(std::move(n));
    }
    auto valid = validateRigDefinition(d);
    if (!valid.ok()) return Result<RigDefinition>::failure(valid.status());
    for (auto& c : d.controls) {
        auto initial = constrainRigControl(c.limits, c.initial);
        if (!initial.ok()) return Result<RigDefinition>::failure(initial.status());
        c.initial = initial.value();
        for (auto& k : c.keys) {
            auto projected = constrainRigControl(c.limits, k.transform);
            if (!projected.ok()) return Result<RigDefinition>::failure(projected.status());
            k.transform = projected.value();
        }
    }
    return Result<RigDefinition>::success(std::move(d));
}
}  // namespace eve::animation
