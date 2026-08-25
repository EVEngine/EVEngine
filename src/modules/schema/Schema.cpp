#include "schema/Schema.h"

#include "schema/SchemaRegistry.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>

namespace eve::schema {

Module_IMPL(Schema, new Schema());

namespace {

/** @brief The schema type implied by a Squirrel slot type. */
ValueType typeFromSquirrel(ssq::Type type) {
    switch (type) {
        case ssq::Type::INTEGER: return ValueType::Integer;
        case ssq::Type::FLOAT: return ValueType::Number;
        case ssq::Type::BOOL: return ValueType::Boolean;
        case ssq::Type::STRING: return ValueType::String;
        case ssq::Type::ARRAY: return ValueType::Array;
        case ssq::Type::TABLE: return ValueType::Object;
        default: return ValueType::Any;
    }
}

/** @brief Renders a Squirrel scalar at `index` as JSON text; empty otherwise. */
std::string scalarToJson(HSQUIRRELVM vm, SQInteger index) {
    switch (sq_gettype(vm, index)) {
        case OT_INTEGER: {
            SQInteger value = 0;
            if (SQ_SUCCEEDED(sq_getinteger(vm, index, &value))) return std::to_string(value);
            break;
        }
        case OT_FLOAT: {
            SQFloat value = 0;
            if (SQ_SUCCEEDED(sq_getfloat(vm, index, &value))) {
                char buffer[64];
                std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
                return buffer;
            }
            break;
        }
        case OT_BOOL: {
            SQBool value = SQFalse;
            if (SQ_SUCCEEDED(sq_getbool(vm, index, &value))) return value ? "true" : "false";
            break;
        }
        case OT_STRING: {
            const SQChar* value = nullptr;
            if (SQ_SUCCEEDED(sq_getstring(vm, index, &value)) && value) {
                std::string out = "\"";
                for (const SQChar* p = value; *p; ++p) {
                    if (*p == '"' || *p == '\\') out += '\\';
                    out += *p;
                }
                out += '"';
                return out;
            }
            break;
        }
        default: break;
    }
    return {};
}

bool getAttrString(HSQUIRRELVM vm, int tableIdx, const char* key, std::string& out) {
    const SQInteger top = sq_gettop(vm);
    sq_push(vm, tableIdx);
    sq_pushstring(vm, key, static_cast<SQInteger>(std::strlen(key)));
    bool found = false;
    if (SQ_SUCCEEDED(sq_get(vm, -2)) && sq_gettype(vm, -1) == OT_STRING) {
        const SQChar* value = nullptr;
        if (SQ_SUCCEEDED(sq_getstring(vm, -1, &value)) && value) {
            out = value;
            found = true;
        }
    }
    sq_settop(vm, top);
    return found;
}

bool getAttrBool(HSQUIRRELVM vm, int tableIdx, const char* key, bool& out) {
    const SQInteger top = sq_gettop(vm);
    sq_push(vm, tableIdx);
    sq_pushstring(vm, key, static_cast<SQInteger>(std::strlen(key)));
    bool found = false;
    if (SQ_SUCCEEDED(sq_get(vm, -2)) && sq_gettype(vm, -1) == OT_BOOL) {
        SQBool value = SQFalse;
        if (SQ_SUCCEEDED(sq_getbool(vm, -1, &value))) {
            out = value != SQFalse;
            found = true;
        }
    }
    sq_settop(vm, top);
    return found;
}

bool getAttrInt(HSQUIRRELVM vm, int tableIdx, const char* key, int& out) {
    const SQInteger top = sq_gettop(vm);
    sq_push(vm, tableIdx);
    sq_pushstring(vm, key, static_cast<SQInteger>(std::strlen(key)));
    bool found = false;
    if (SQ_SUCCEEDED(sq_get(vm, -2)) && sq_gettype(vm, -1) == OT_INTEGER) {
        SQInteger value = 0;
        if (SQ_SUCCEEDED(sq_getinteger(vm, -1, &value))) {
            out = static_cast<int>(value);
            found = true;
        }
    }
    sq_settop(vm, top);
    return found;
}

bool getAttrNumber(HSQUIRRELVM vm, int tableIdx, const char* key, double& out) {
    const SQInteger top = sq_gettop(vm);
    sq_push(vm, tableIdx);
    sq_pushstring(vm, key, static_cast<SQInteger>(std::strlen(key)));
    bool found = false;
    if (SQ_SUCCEEDED(sq_get(vm, -2)) && sq_gettype(vm, -1) == OT_INTEGER) {
        SQInteger value = 0;
        if (SQ_SUCCEEDED(sq_getinteger(vm, -1, &value))) {
            out = static_cast<double>(value);
            found = true;
        }
    } else if (SQ_SUCCEEDED(sq_get(vm, -2)) && sq_gettype(vm, -1) == OT_FLOAT) {
        SQFloat value = 0;
        if (SQ_SUCCEEDED(sq_getfloat(vm, -1, &value))) {
            out = static_cast<double>(value);
            found = true;
        }
    }
    sq_settop(vm, top);
    return found;
}

void getAttrStringArray(HSQUIRRELVM vm, int tableIdx, const char* key, std::vector<std::string>& out) {
    const SQInteger top = sq_gettop(vm);
    sq_push(vm, tableIdx);
    sq_pushstring(vm, key, static_cast<SQInteger>(std::strlen(key)));
    if (SQ_SUCCEEDED(sq_get(vm, -2)) && sq_gettype(vm, -1) == OT_ARRAY) {
        const SQInteger arrayIdx = sq_gettop(vm);
        const SQInteger length   = sq_getsize(vm, arrayIdx);
        for (SQInteger i = 0; i < length; ++i) {
            sq_pushinteger(vm, i);
            if (SQ_SUCCEEDED(sq_get(vm, arrayIdx)) && sq_gettype(vm, -1) == OT_STRING) {
                const SQChar* value = nullptr;
                if (SQ_SUCCEEDED(sq_getstring(vm, -1, &value)) && value) out.push_back(value);
            }
            sq_pop(vm, 1);
        }
    }
    sq_settop(vm, top);
}

void applyFieldAttributes(HSQUIRRELVM vm, int attrsIdx, FieldDefinition& field) {
    std::string text;
    if (getAttrString(vm, attrsIdx, "type", text))
        if (auto type = valueTypeFromString(text)) field.type = *type;
    if (getAttrString(vm, attrsIdx, "elementType", text))
        if (auto type = valueTypeFromString(text)) field.elementType = *type;
    if (getAttrString(vm, attrsIdx, "title", text)) field.title = text;
    if (getAttrString(vm, attrsIdx, "description", text)) field.description = text;
    if (getAttrString(vm, attrsIdx, "reference", text)) field.reference = text;
    if (getAttrString(vm, attrsIdx, "defaultJson", text)) field.defaultJson = text;
    double number = 0.0;
    if (getAttrNumber(vm, attrsIdx, "minimum", number) || getAttrNumber(vm, attrsIdx, "min", number))
        field.minimum = number;
    if (getAttrNumber(vm, attrsIdx, "maximum", number) || getAttrNumber(vm, attrsIdx, "max", number))
        field.maximum = number;
    int integer = 0;
    if (getAttrInt(vm, attrsIdx, "minLength", integer)) field.minLength = integer;
    if (getAttrInt(vm, attrsIdx, "maxLength", integer)) field.maxLength = integer;
    bool boolean = false;
    if (getAttrBool(vm, attrsIdx, "required", boolean)) field.required = boolean;
    getAttrStringArray(vm, attrsIdx, "values", field.enumValues);
    if (field.enumValues.empty()) getAttrStringArray(vm, attrsIdx, "enum", field.enumValues);
}

}  // namespace

bool Schema::registerJson(const std::string& json) {
    lastError_.clear();
    return SchemaRegistry::registerFromJson(json, &lastError_);
}

bool Schema::registerFromClass(const ssq::Object& classOrInstance) {
    lastError_.clear();
    const ssq::Type type = classOrInstance.getType();
    if (type != ssq::Type::CLASS && type != ssq::Type::INSTANCE) {
        lastError_ = "registerFromClass expects a class or an instance of a class";
        return false;
    }
    HSQUIRRELVM     vm  = classOrInstance.getHandle();
    const SQInteger top = sq_gettop(vm);

    sq_pushobject(vm, classOrInstance.getRaw());
    if (type == ssq::Type::INSTANCE && SQ_FAILED(sq_getclass(vm, -1))) {
        sq_settop(vm, top);
        lastError_ = "could not resolve the instance's class";
        return false;
    }
    const SQInteger clsIdx = sq_gettop(vm);

    SchemaDefinition definition;
    const SQInteger metaTop = sq_gettop(vm);
    sq_push(vm, clsIdx);
    sq_pushnull(vm);  // null key selects class-level attributes
    if (SQ_SUCCEEDED(sq_getattributes(vm, -2)) && sq_gettype(vm, -1) == OT_TABLE) {
        const SQInteger attrsIdx = sq_gettop(vm);
        std::string     meta;
        if (getAttrString(vm, attrsIdx, "id", meta)) definition.id = meta;
        if (getAttrString(vm, attrsIdx, "title", meta)) definition.title = meta;
        if (getAttrString(vm, attrsIdx, "description", meta)) definition.description = meta;
        int version = 1;
        if (getAttrInt(vm, attrsIdx, "version", version)) definition.version = version;
        bool strict = false;
        if (getAttrBool(vm, attrsIdx, "additionalProperties", strict)) definition.additionalProperties = strict;
    }
    sq_settop(vm, metaTop);

    if (definition.id.empty()) {
        sq_settop(vm, top);
        lastError_ = "schema id is required (set the class-level attribute 'id')";
        return false;
    }

    std::unordered_set<std::string> names;
    sq_push(vm, clsIdx);
    sq_pushnull(vm);
    while (SQ_SUCCEEDED(sq_next(vm, -2))) {
        if (sq_gettype(vm, -2) != OT_STRING) {
            sq_pop(vm, 2);
            continue;
        }
        const SQChar* memberName = nullptr;
        if (!SQ_SUCCEEDED(sq_getstring(vm, -2, &memberName)) || !memberName) {
            sq_pop(vm, 2);
            continue;
        }
        const SQObjectType memberType = sq_gettype(vm, -1);
        if (memberType == OT_CLOSURE || memberType == OT_NATIVECLOSURE) {
            sq_pop(vm, 2);
            continue;
        }
        const std::string name(memberName);
        if (!names.insert(name).second) {
            sq_pop(vm, 2);
            continue;
        }

        FieldDefinition field;
        field.name        = name;
        field.type        = typeFromSquirrel(static_cast<ssq::Type>(memberType));
        field.defaultJson = scalarToJson(vm, -1);

        const SQInteger attrsTop = sq_gettop(vm);
        sq_push(vm, clsIdx);
        sq_pushstring(vm, field.name.c_str(), static_cast<SQInteger>(field.name.size()));
        if (SQ_SUCCEEDED(sq_getattributes(vm, -2)) && sq_gettype(vm, -1) == OT_TABLE) {
            applyFieldAttributes(vm, sq_gettop(vm), field);
        }
        sq_settop(vm, attrsTop);

        definition.fields.push_back(std::move(field));
        sq_pop(vm, 2);
    }
    sq_settop(vm, top);

    return SchemaRegistry::registerSchema(definition, &lastError_);
}

void Schema::clear() {
    SchemaRegistry::clear();
    lastError_.clear();
    validationErrors_.clear();
}

bool Schema::has(const std::string& id) const { return SchemaRegistry::find(id) != nullptr; }
int  Schema::getSchemaCount() const { return SchemaRegistry::count(); }

std::string Schema::getSchemaId(int index) const {
    const auto values = SchemaRegistry::ids();
    return index >= 0 && static_cast<size_t>(index) < values.size() ? values[index] : std::string{};
}

int Schema::getSchemaVersion(const std::string& id) const {
    const auto* value = SchemaRegistry::find(id);
    return value ? value->version : 0;
}

std::string Schema::getSchemaTitle(const std::string& id) const {
    const auto* value = SchemaRegistry::find(id);
    return value ? value->title : std::string{};
}

std::string Schema::getSchemaDescription(const std::string& id) const {
    const auto* value = SchemaRegistry::find(id);
    return value ? value->description : std::string{};
}

bool Schema::getSchemaAdditionalProperties(const std::string& id) const {
    const auto* value = SchemaRegistry::find(id);
    return value && value->additionalProperties;
}

std::string Schema::getLastError() const { return lastError_; }

const FieldDefinition* Schema::field(const std::string& id, int index) const {
    const auto* value = SchemaRegistry::find(id);
    if (!value || index < 0 || static_cast<size_t>(index) >= value->fields.size()) return nullptr;
    return &value->fields[static_cast<size_t>(index)];
}

int Schema::getFieldCount(const std::string& id) const {
    const auto* value = SchemaRegistry::find(id);
    return value ? static_cast<int>(value->fields.size()) : 0;
}

std::string Schema::getFieldName(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value ? value->name : std::string{};
}
std::string Schema::getFieldType(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value ? valueTypeName(value->type) : std::string{};
}
std::string Schema::getFieldElementType(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value ? valueTypeName(value->elementType) : std::string{};
}
bool Schema::getFieldRequired(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value && value->required;
}
std::string Schema::getFieldTitle(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value ? value->title : std::string{};
}
std::string Schema::getFieldDescription(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value ? value->description : std::string{};
}
std::string Schema::getFieldReference(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value ? value->reference : std::string{};
}
std::string Schema::getFieldDefaultJson(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value ? value->defaultJson : std::string{};
}
bool Schema::getFieldHasMinimum(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value && value->minimum.has_value();
}
float Schema::getFieldMinimum(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value && value->minimum ? static_cast<float>(*value->minimum) : 0.f;
}
bool Schema::getFieldHasMaximum(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value && value->maximum.has_value();
}
float Schema::getFieldMaximum(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value && value->maximum ? static_cast<float>(*value->maximum) : 0.f;
}
int Schema::getFieldEnumCount(const std::string& id, int index) const {
    const auto* value = field(id, index);
    return value ? static_cast<int>(value->enumValues.size()) : 0;
}
std::string Schema::getFieldEnumValue(const std::string& id, int fieldIndex, int valueIndex) const {
    const auto* value = field(id, fieldIndex);
    if (!value || valueIndex < 0 || static_cast<size_t>(valueIndex) >= value->enumValues.size()) return {};
    return value->enumValues[static_cast<size_t>(valueIndex)];
}

bool Schema::validateJson(const std::string& schemaId, const std::string& json) {
    validationErrors_ = SchemaRegistry::validate(schemaId, json);
    return validationErrors_.empty();
}
int         Schema::getValidationErrorCount() const { return static_cast<int>(validationErrors_.size()); }
std::string Schema::getValidationErrorPath(int index) const {
    return index >= 0 && static_cast<size_t>(index) < validationErrors_.size()
               ? validationErrors_[static_cast<size_t>(index)].path
               : std::string{};
}
std::string Schema::getValidationErrorCode(int index) const {
    return index >= 0 && static_cast<size_t>(index) < validationErrors_.size()
               ? validationErrors_[static_cast<size_t>(index)].code
               : std::string{};
}
std::string Schema::getValidationErrorMessage(int index) const {
    return index >= 0 && static_cast<size_t>(index) < validationErrors_.size()
               ? validationErrors_[static_cast<size_t>(index)].message
               : std::string{};
}

void Schema::expose(ssq::Table& table) {
    auto cls = table.addClass(name, Schema::create, false);
    expose(cls);
}

void Schema::expose(ssq::Class& cls) {
    cls.addFunc("getName", &Schema::getName);
    cls.addFunc("registerJson", &Schema::registerJson);
    cls.addFunc("registerFromClass", &Schema::registerFromClass);
    cls.addFunc("clear", &Schema::clear);
    cls.addFunc("has", &Schema::has);
    cls.addFunc("getSchemaCount", &Schema::getSchemaCount);
    cls.addFunc("getSchemaId", &Schema::getSchemaId);
    cls.addFunc("getSchemaVersion", &Schema::getSchemaVersion);
    cls.addFunc("getSchemaTitle", &Schema::getSchemaTitle);
    cls.addFunc("getSchemaDescription", &Schema::getSchemaDescription);
    cls.addFunc("getSchemaAdditionalProperties", &Schema::getSchemaAdditionalProperties);
    cls.addFunc("getLastError", &Schema::getLastError);
    cls.addFunc("getFieldCount", &Schema::getFieldCount);
    cls.addFunc("getFieldName", &Schema::getFieldName);
    cls.addFunc("getFieldType", &Schema::getFieldType);
    cls.addFunc("getFieldElementType", &Schema::getFieldElementType);
    cls.addFunc("getFieldRequired", &Schema::getFieldRequired);
    cls.addFunc("getFieldTitle", &Schema::getFieldTitle);
    cls.addFunc("getFieldDescription", &Schema::getFieldDescription);
    cls.addFunc("getFieldReference", &Schema::getFieldReference);
    cls.addFunc("getFieldDefaultJson", &Schema::getFieldDefaultJson);
    cls.addFunc("getFieldHasMinimum", &Schema::getFieldHasMinimum);
    cls.addFunc("getFieldMinimum", &Schema::getFieldMinimum);
    cls.addFunc("getFieldHasMaximum", &Schema::getFieldHasMaximum);
    cls.addFunc("getFieldMaximum", &Schema::getFieldMaximum);
    cls.addFunc("getFieldEnumCount", &Schema::getFieldEnumCount);
    cls.addFunc("getFieldEnumValue", &Schema::getFieldEnumValue);
    cls.addFunc("validateJson", &Schema::validateJson);
    cls.addFunc("getValidationErrorCount", &Schema::getValidationErrorCount);
    cls.addFunc("getValidationErrorPath", &Schema::getValidationErrorPath);
    cls.addFunc("getValidationErrorCode", &Schema::getValidationErrorCode);
    cls.addFunc("getValidationErrorMessage", &Schema::getValidationErrorMessage);
}

}  // namespace eve::schema
