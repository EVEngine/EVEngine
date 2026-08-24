#include "editor/EditorValueJson.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <type_traits>

namespace eve::editor {
namespace {

Poco::Dynamic::Var toPoco(const EditorValue& value) {
    return std::visit(
        [](const auto& current) -> Poco::Dynamic::Var {
            using T = std::decay_t<decltype(current)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return Poco::Dynamic::Var{};
            } else if constexpr (std::is_same_v<T, EditorValue::Array>) {
                Poco::JSON::Array::Ptr array(new Poco::JSON::Array());
                for (const EditorValue& entry : current) array->add(toPoco(entry));
                return Poco::Dynamic::Var(array);
            } else if constexpr (std::is_same_v<T, EditorValue::Object>) {
                Poco::JSON::Object::Ptr object(new Poco::JSON::Object());
                for (const auto& [key, entry] : current) object->set(key, toPoco(entry));
                return Poco::Dynamic::Var(object);
            } else {
                return Poco::Dynamic::Var(current);
            }
        },
        value.storage());
}

bool fromPoco(const Poco::Dynamic::Var& value, EditorValue& out, std::size_t depth = 0) {
    if (depth > 64) return false;
    if (value.isEmpty()) {
        out = EditorValue{};
        return true;
    }
    if (value.type() == typeid(Poco::JSON::Object::Ptr)) {
        const Poco::JSON::Object::Ptr object = value.extract<Poco::JSON::Object::Ptr>();
        EditorValue::Object           result;
        for (const std::string& key : object->getNames()) {
            EditorValue child;
            if (!fromPoco(object->get(key), child, depth + 1)) return false;
            result.emplace(key, std::move(child));
        }
        out = EditorValue(std::move(result));
        return true;
    }
    if (value.type() == typeid(Poco::JSON::Array::Ptr)) {
        const Poco::JSON::Array::Ptr array = value.extract<Poco::JSON::Array::Ptr>();
        EditorValue::Array           result;
        result.reserve(array->size());
        for (std::size_t i = 0; i < array->size(); ++i) {
            EditorValue child;
            if (!fromPoco(array->get(static_cast<unsigned>(i)), child, depth + 1)) return false;
            result.push_back(std::move(child));
        }
        out = EditorValue(std::move(result));
        return true;
    }
    if (value.isBoolean()) {
        out = EditorValue(value.convert<bool>());
        return true;
    }
    if (value.isInteger() || value.isSigned()) {
        out = EditorValue(value.convert<std::int64_t>());
        return true;
    }
    if (value.isNumeric()) {
        out = EditorValue(value.convert<double>());
        return true;
    }
    if (value.isString()) {
        out = EditorValue(value.convert<std::string>());
        return true;
    }
    return false;
}

std::uint64_t fnv1a(const std::string& text) {
    std::uint64_t hash = 1469598103934665603ull;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

}  // namespace

std::string editorValueToJson(const EditorValue& value) {
    std::ostringstream stream;
    Poco::JSON::Stringifier::stringify(toPoco(value), stream, 0, -1);
    return stream.str();
}

EditorResult<EditorValue> editorValueFromJson(const std::string& json) {
    try {
        Poco::JSON::Parser parser;
        EditorValue        value;
        if (!fromPoco(parser.parse(json), value))
            return EditorResult<EditorValue>::error(EditorStatus::Rejected, RuleId("editor.value.unsupported-json"),
                                                    "JSON contains a value outside the EditorValue protocol");
        return EditorResult<EditorValue>::applied(std::move(value));
    } catch (const std::exception& error) {
        return EditorResult<EditorValue>::error(EditorStatus::Rejected, RuleId("editor.value.invalid-json"),
                                                error.what());
    }
}

std::string editorValueContentHash(const EditorValue& value) {
    std::ostringstream stream;
    stream << std::hex << std::setfill('0') << std::setw(16) << fnv1a(editorValueToJson(value));
    return stream.str();
}

}  // namespace eve::editor
