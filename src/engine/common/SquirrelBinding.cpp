#include "common/SquirrelBinding.h"

#include "common/Assert.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace eve::script {
namespace {

struct ConversionContext {
    const SquirrelValueOptions& options;
    std::size_t elements = 0;
    std::vector<const void*> activeContainers;
};

Diagnostic conversionError(const ConversionContext& context, DiagnosticCode code,
                           std::string message, std::string path) {
    return Diagnostic::error(code, std::move(message), std::move(path), {}, context.options.source);
}

std::string childPath(std::string_view parent, std::size_t index) {
    return std::string(parent) + "[" + std::to_string(index) + "]";
}

std::string memberPath(std::string_view parent, std::string_view key) {
    if (!key.empty() && (std::isalpha(static_cast<unsigned char>(key.front())) != 0 ||
                         key.front() == '_') &&
        std::all_of(key.begin() + 1, key.end(), [](unsigned char value) {
            return std::isalnum(value) != 0 || value == '_';
        })) {
        return std::string(parent) + "." + std::string(key);
    }
    return std::string(parent) + "[\"" + std::string(key) + "\"]";
}

bool takeObject(HSQUIRRELVM vm, SQInteger index, HSQOBJECT& object,
                Diagnostic& diagnostic, const ConversionContext& context,
                const std::string& path) {
    if (!vm || SQ_FAILED(sq_getstackobj(vm, index, &object))) {
        diagnostic = conversionError(context, DiagnosticCode::InvalidArgument,
                                     "Squirrel stack value is unavailable", path);
        return false;
    }
    return true;
}

bool enterContainer(const HSQOBJECT& object, ConversionContext& context,
                    Diagnostic& diagnostic, const std::string& path) {
    if (context.activeContainers.size() >= context.options.maxDepth) {
        diagnostic = conversionError(context, DiagnosticCode::InvalidArgument,
                                     "Squirrel value exceeds maximum conversion depth", path);
        return false;
    }
    const void* identity = nullptr;
    if (object._type == OT_ARRAY)
        identity = static_cast<const void*>(object._unVal.pArray);
    else if (object._type == OT_TABLE)
        identity = static_cast<const void*>(object._unVal.pTable);
    if (identity && std::find(context.activeContainers.begin(),
                              context.activeContainers.end(), identity) !=
                       context.activeContainers.end()) {
        diagnostic = conversionError(context, DiagnosticCode::InvalidArgument,
                                     "cyclic Squirrel container cannot become an owning Value", path);
        return false;
    }
    if (identity) context.activeContainers.push_back(identity);
    return true;
}

void leaveContainer(const HSQOBJECT& object, ConversionContext& context) {
    if ((object._type != OT_ARRAY && object._type != OT_TABLE) ||
        context.activeContainers.empty())
        return;
    context.activeContainers.pop_back();
}

bool countElement(ConversionContext& context, Diagnostic& diagnostic,
                  const std::string& path) {
    if (context.elements >= context.options.maxElements) {
        diagnostic = conversionError(context, DiagnosticCode::InvalidArgument,
                                     "Squirrel value exceeds maximum element count", path);
        return false;
    }
    ++context.elements;
    return true;
}

bool convertAt(HSQUIRRELVM vm, SQInteger index, ConversionContext& context,
               const std::string& path, Value& output, Diagnostic& diagnostic) {
    if (!countElement(context, diagnostic, path)) return false;

    const SQObjectType type = sq_gettype(vm, index);
    switch (type) {
    case OT_NULL:
        output = Value();
        return true;
    case OT_BOOL: {
        SQBool value = SQFalse;
        if (SQ_FAILED(sq_getbool(vm, index, &value))) {
            diagnostic = conversionError(context, DiagnosticCode::ParseError,
                                         "failed to read Squirrel boolean", path);
            return false;
        }
        output = Value(value != SQFalse);
        return true;
    }
    case OT_INTEGER: {
        SQInteger value = 0;
        if (SQ_FAILED(sq_getinteger(vm, index, &value))) {
            diagnostic = conversionError(context, DiagnosticCode::ParseError,
                                         "failed to read Squirrel integer", path);
            return false;
        }
        output = Value(static_cast<std::int64_t>(value));
        return true;
    }
    case OT_FLOAT: {
        SQFloat value = 0;
        if (SQ_FAILED(sq_getfloat(vm, index, &value))) {
            diagnostic = conversionError(context, DiagnosticCode::ParseError,
                                         "failed to read Squirrel number", path);
            return false;
        }
        const double converted = static_cast<double>(value);
        if (!std::isfinite(converted)) {
            diagnostic = conversionError(context, DiagnosticCode::InvalidArgument,
                                         "non-finite Squirrel number is not representable", path);
            return false;
        }
        output = Value(converted);
        return true;
    }
    case OT_STRING: {
        const SQChar* value = nullptr;
        SQInteger size = 0;
        if (SQ_FAILED(sq_getstringandsize(vm, index, &value, &size)) || !value || size < 0) {
            diagnostic = conversionError(context, DiagnosticCode::ParseError,
                                         "failed to read Squirrel string", path);
            return false;
        }
        output = Value(std::string(value, static_cast<std::size_t>(size)));
        return true;
    }
    case OT_ARRAY:
    case OT_TABLE:
        break;
    default:
        diagnostic = conversionError(context, DiagnosticCode::Unsupported,
                                     "Squirrel value kind is not part of eve::Value", path);
        return false;
    }

    HSQOBJECT object{};
    if (!takeObject(vm, index, object, diagnostic, context, path)) return false;
    if (!enterContainer(object, context, diagnostic, path)) return false;

    bool converted = true;
    if (type == OT_ARRAY) {
        const SQInteger size = sq_getsize(vm, index);
        if (size < 0) {
            diagnostic = conversionError(context, DiagnosticCode::ParseError,
                                         "failed to read Squirrel array size", path);
            converted = false;
        } else {
            Value::Array values;
            values.reserve(static_cast<std::size_t>(size));
            const SQInteger absolute = index > 0 ? index : sq_gettop(vm) + index + 1;
            for (SQInteger i = 0; converted && i < size; ++i) {
                sq_pushinteger(vm, i);
                if (SQ_FAILED(sq_get(vm, absolute))) {
                    diagnostic = conversionError(context, DiagnosticCode::ParseError,
                                                 "failed to read Squirrel array element",
                                                 childPath(path, static_cast<std::size_t>(i)));
                    converted = false;
                    break;
                }
                Value element;
                converted = convertAt(vm, -1, context,
                                      childPath(path, static_cast<std::size_t>(i)), element,
                                      diagnostic);
                sq_pop(vm, 1);
                if (converted) values.push_back(std::move(element));
            }
            if (converted) output = Value(std::move(values));
        }
    } else {
        Value::Object fields;
        const SQInteger absolute = index > 0 ? index : sq_gettop(vm) + index + 1;
        sq_pushnull(vm);
        while (converted && SQ_SUCCEEDED(sq_next(vm, absolute))) {
            if (sq_gettype(vm, -2) != OT_STRING) {
                diagnostic = conversionError(context, DiagnosticCode::Unsupported,
                                             "Squirrel table keys must be strings",
                                             path + "[<non-string-key>]");
                converted = false;
                sq_pop(vm, 2);
                break;
            }
            const SQChar* key = nullptr;
            SQInteger keySize = 0;
            if (SQ_FAILED(sq_getstringandsize(vm, -2, &key, &keySize)) || !key || keySize < 0) {
                diagnostic = conversionError(context, DiagnosticCode::ParseError,
                                             "failed to read Squirrel table key", path);
                converted = false;
                sq_pop(vm, 2);
                break;
            }
            const std::string keyText(key, static_cast<std::size_t>(keySize));
            const std::string field = memberPath(path, keyText);
            Value value;
            converted = convertAt(vm, -1, context, field, value, diagnostic);
            sq_pop(vm, 2);
            if (converted) fields.emplace(keyText, std::move(value));
        }
        sq_pop(vm, 1);  // iterator
        if (converted) output = Value(std::move(fields));
    }
    leaveContainer(object, context);
    return converted;
}

bool validateValue(const Value& value, const SquirrelValueOptions& options,
                   std::size_t depth, std::size_t& elements, const std::string& path,
                   Diagnostic& diagnostic) {
    if (elements >= options.maxElements) {
        diagnostic = Diagnostic::error(DiagnosticCode::InvalidArgument,
                                       "Value exceeds maximum Squirrel element count", path,
                                       {}, options.source);
        return false;
    }
    ++elements;
    if (value.isDouble() && !std::isfinite(value.asDouble())) {
        diagnostic = Diagnostic::error(DiagnosticCode::InvalidArgument,
                                       "non-finite Value cannot be pushed to Squirrel", path,
                                       {}, options.source);
        return false;
    }
    if (!value.isArray() && !value.isObject()) return true;
    if (depth >= options.maxDepth) {
        diagnostic = Diagnostic::error(DiagnosticCode::InvalidArgument,
                                       "Value exceeds maximum Squirrel conversion depth", path,
                                       {}, options.source);
        return false;
    }
    if (value.isArray()) {
        for (std::size_t index = 0; index < value.arraySize(); ++index) {
            if (!validateValue(value.at(index), options, depth + 1, elements,
                               childPath(path, index), diagnostic))
                return false;
        }
        return true;
    }
    for (const std::string& key : value.keys()) {
        const Value* member = value.find(key);
        if (!member) continue;
        if (!validateValue(*member, options, depth + 1, elements,
                           memberPath(path, key), diagnostic))
            return false;
    }
    return true;
}

bool pushImpl(HSQUIRRELVM vm, const Value& value, const SquirrelValueOptions& options,
              std::size_t depth, const std::string& path, Diagnostic& diagnostic) {
    if (value.isNull()) {
        sq_pushnull(vm);
        return true;
    }
    if (value.isBool()) {
        sq_pushbool(vm, value.asBool() ? SQTrue : SQFalse);
        return true;
    }
    if (value.isInt64()) {
        const auto integer = value.asInt();
        if (integer < static_cast<std::int64_t>(std::numeric_limits<SQInteger>::min()) ||
            integer > static_cast<std::int64_t>(std::numeric_limits<SQInteger>::max())) {
            diagnostic = Diagnostic::error(DiagnosticCode::InvalidArgument,
                                           "Int64 value does not fit Squirrel integer", path,
                                           {}, options.source);
            return false;
        }
        sq_pushinteger(vm, static_cast<SQInteger>(integer));
        return true;
    }
    if (value.isDouble()) {
        sq_pushfloat(vm, static_cast<SQFloat>(value.asDouble()));
        return true;
    }
    if (value.isString()) {
        const std::string& string = value.asString();
        sq_pushstring(vm, string.c_str(), static_cast<SQInteger>(string.size()));
        return true;
    }
    if (depth >= options.maxDepth) {
        diagnostic = Diagnostic::error(DiagnosticCode::InvalidArgument,
                                       "Value exceeds maximum Squirrel conversion depth", path,
                                       {}, options.source);
        return false;
    }
    if (value.isArray()) {
        sq_newarray(vm, 0);
        for (std::size_t index = 0; index < value.arraySize(); ++index) {
            if (!pushImpl(vm, value.at(index), options, depth + 1,
                          childPath(path, index), diagnostic))
                return false;
            if (SQ_FAILED(sq_arrayappend(vm, -2))) {
                diagnostic = Diagnostic::error(DiagnosticCode::Failed,
                                               "failed to append Value array element to Squirrel",
                                               childPath(path, index), {}, options.source);
                return false;
            }
        }
        return true;
    }
    sq_newtable(vm);
    for (const std::string& key : value.keys()) {
        const Value* member = value.find(key);
        if (!member) continue;
        sq_pushstring(vm, key.c_str(), static_cast<SQInteger>(key.size()));
        if (!pushImpl(vm, *member, options, depth + 1,
                      memberPath(path, key), diagnostic))
            return false;
        if (SQ_FAILED(sq_newslot(vm, -3, SQFalse))) {
            diagnostic = Diagnostic::error(DiagnosticCode::Failed,
                                           "failed to add Value object member to Squirrel",
                                           memberPath(path, key), {}, options.source);
            return false;
        }
    }
    return true;
}

ssq::Object objectFromTop(HSQUIRRELVM vm, SQInteger top) {
    ssq::Object output(vm);
    if (SQ_SUCCEEDED(sq_getstackobj(vm, -1, &output.getRaw()))) {
        sq_addref(vm, &output.getRaw());
    }
    sq_settop(vm, top);
    return output;
}

ssq::Object projectValueObject(HSQUIRRELVM vm, const Value& value) {
    if (!vm) return ssq::Object();
    const SQInteger top = sq_gettop(vm);
    auto pushed = pushValue(vm, value);
    if (!pushed.ok()) {
        pushed.ignore("Result projection could not push its value payload");
        sq_settop(vm, top);
        return ssq::Object(vm);
    }
    return objectFromTop(vm, top);
}

bool readBoolField(const ssq::Object& object, const char* name) {
    if (object.getType() != ssq::Type::TABLE) return false;
    const ssq::Object field = object.find(name);
    return field.getType() == ssq::Type::BOOL && field.toBool();
}

}  // namespace

Result<Value> valueFromSquirrel(HSQUIRRELVM vm, SQInteger index,
                                 const SquirrelValueOptions& options) {
    if (!vm)
        return Result<Value>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Squirrel VM must not be null", "$", {}, options.source));
    const SQInteger top = sq_gettop(vm);
    ConversionContext context{options, 0, {}};
    Value output;
    Diagnostic diagnostic;
    const bool converted = convertAt(vm, index, context, "$", output, diagnostic);
    sq_settop(vm, top);
    if (!converted) return Result<Value>::failure(std::move(diagnostic));
    return Result<Value>::success(std::move(output));
}

Result<Value> valueFromSquirrel(const ssq::Object& object,
                                 const SquirrelValueOptions& options) {
    const HSQUIRRELVM vm = object.getHandle();
    if (!vm || object.isEmpty())
        return Result<Value>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Squirrel object is empty", "$", {}, options.source));
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, object.getRaw());
    auto result = valueFromSquirrel(vm, -1, options);
    // valueFromSquirrel restores to the height observed after this push.
    sq_settop(vm, top);
    return result;
}

Result<void> pushValue(HSQUIRRELVM vm, const Value& value,
                       const SquirrelValueOptions& options) {
    if (!vm)
        return Result<void>::failure(Diagnostic::error(
            DiagnosticCode::InvalidArgument, "Squirrel VM must not be null", "$", {}, options.source));
    std::size_t elements = 0;
    Diagnostic diagnostic;
    if (!validateValue(value, options, 0, elements, "$", diagnostic))
        return Result<void>::failure(std::move(diagnostic));

    const SQInteger top = sq_gettop(vm);
    if (!pushImpl(vm, value, options, 0, "$", diagnostic)) {
        sq_settop(vm, top);
        return Result<void>::failure(std::move(diagnostic));
    }
    return Result<void>::success();
}

ssq::Table projectDiagnostic(HSQUIRRELVM vm, const Diagnostic& diagnostic) {
    ssq::Table result(vm);
    result.set("code", std::string(diagnosticCodeName(diagnostic.code())));
    result.set("severity", [&diagnostic] {
        switch (diagnostic.severity()) {
        case Severity::Info: return std::string("info");
        case Severity::Warning: return std::string("warning");
        case Severity::Error: return std::string("error");
        case Severity::Fatal: return std::string("fatal");
        }
        return std::string("unknown");
    }());
    result.set("message", diagnostic.message());
    result.set("path", diagnostic.path());
    result.set("source", diagnostic.source());
    ssq::Table details(vm);
    for (const auto& [key, value] : diagnostic.details()) details.set(key.c_str(), value);
    result.set("details", details);
    return result;
}

ssq::Table projectStatus(HSQUIRRELVM vm, const Status& status) {
    ssq::Table result(vm);
    result.set("ok", status.isSuccess());
    result.set("code", std::string(statusCodeName(status.code())));
    result.set("summary", status.describe());
    ssq::Array diagnostics(vm);
    for (const Diagnostic& diagnostic : status.diagnostics())
        diagnostics.push(projectDiagnostic(vm, diagnostic));
    result.set("diagnostics", diagnostics);
    result.set("diagnosticCount", static_cast<std::int64_t>(status.diagnostics().size()));
    return result;
}

ssq::Table projectStatusResult(HSQUIRRELVM vm, const Status& status,
                               bool ok, bool hasValue, const Value& value) {
    ssq::Table result(vm);
    result.set("ok", ok);
    result.set("code", std::string(statusCodeName(status.code())));
    result.set("hasValue", hasValue);
    result.set("checked", true);
    result.set("ignored", false);
    result.set("ignoreReason", std::string{});
    result.set("status", projectStatus(vm, status));
    ssq::Array diagnostics(vm);
    for (const Diagnostic& diagnostic : status.diagnostics())
        diagnostics.push(projectDiagnostic(vm, diagnostic));
    result.set("diagnostics", diagnostics);
    if (hasValue) result.set("value", projectValueObject(vm, value));
    else result.set("value", ssq::Object(vm));
    return result;
}

ssq::Table projectResult(HSQUIRRELVM vm, Result<void>&& result) {
    const bool ok = result.ok();
    const Status status = result.status();
    return projectStatusResult(vm, status, ok, false);
}

bool ignoreResult(const ssq::Object& result, const std::string& reason) {
    if (result.getType() != ssq::Type::TABLE || reason.empty()) return false;
    HSQUIRRELVM vm = result.getHandle();
    if (!vm) return false;
    const SQInteger top = sq_gettop(vm);
    sq_pushobject(vm, result.getRaw());
    sq_pushstring(vm, "ignored", -1);
    sq_pushbool(vm, SQTrue);
    if (SQ_FAILED(sq_newslot(vm, -3, SQFalse))) {
        sq_settop(vm, top);
        return false;
    }
    sq_pushstring(vm, "ignoreReason", -1);
    sq_pushstring(vm, reason.c_str(), static_cast<SQInteger>(reason.size()));
    if (SQ_FAILED(sq_newslot(vm, -3, SQFalse))) {
        sq_settop(vm, top);
        return false;
    }
    // A result table is already consumed by C++; this flag makes the script
    // intent explicit without pretending that ignored means successful.
    sq_pushstring(vm, "checked", -1);
    sq_pushbool(vm, SQTrue);
    if (SQ_FAILED(sq_newslot(vm, -3, SQFalse))) {
        sq_settop(vm, top);
        return false;
    }
    sq_settop(vm, top);
    return true;
}

void exposeResultBindings(ssq::Table& eveTable) {
    ssq::Table result = eveTable.addTable("result");
    result.addFunc("ignore", [](ssq::Object value, const std::string& reason) {
        return ignoreResult(value, reason);
    });
    result.addFunc("isChecked", [](ssq::Object value) {
        return readBoolField(value, "checked");
    });
    result.addFunc("isIgnored", [](ssq::Object value) {
        return readBoolField(value, "ignored");
    });
}

}  // namespace eve::script
