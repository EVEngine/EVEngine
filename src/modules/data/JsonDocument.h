#pragma once

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

namespace eve {
namespace data {

class JsonDocument {
public:
    JsonDocument();
    explicit JsonDocument(Poco::Dynamic::Var root);
    JsonDocument(JsonDocument&&) noexcept;
    JsonDocument& operator=(JsonDocument&&) noexcept;
    JsonDocument(const JsonDocument&) = delete;
    ~JsonDocument() = default;

    bool empty() const;
    bool isObject() const;
    bool isArray() const;

    Poco::Dynamic::Var&       root();
    const Poco::Dynamic::Var& root() const;

    Poco::JSON::Object::Ptr object();
    Poco::JSON::Array::Ptr  array();

private:
    Poco::Dynamic::Var root_;
};

}  // namespace data
}  // namespace eve
