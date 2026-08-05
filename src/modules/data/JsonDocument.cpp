#include "JsonDocument.h"

namespace eve {
namespace data {

JsonDocument::JsonDocument() : root_(Poco::JSON::Object::Ptr(new Poco::JSON::Object)) {}

JsonDocument::JsonDocument(Poco::Dynamic::Var root) : root_(std::move(root)) {}

JsonDocument::JsonDocument(JsonDocument&&) noexcept            = default;
JsonDocument& JsonDocument::operator=(JsonDocument&&) noexcept = default;

bool JsonDocument::empty() const { return root_.isEmpty(); }

bool JsonDocument::isObject() const { return root_.type() == typeid(Poco::JSON::Object::Ptr); }

bool JsonDocument::isArray() const { return root_.type() == typeid(Poco::JSON::Array::Ptr); }

Poco::Dynamic::Var&       JsonDocument::root() { return root_; }
const Poco::Dynamic::Var& JsonDocument::root() const { return root_; }

Poco::JSON::Object::Ptr JsonDocument::object() {
    if (!isObject()) return nullptr;
    return root_.extract<Poco::JSON::Object::Ptr>();
}

Poco::JSON::Array::Ptr JsonDocument::array() {
    if (!isArray()) return nullptr;
    return root_.extract<Poco::JSON::Array::Ptr>();
}

}  // namespace data
}  // namespace eve
