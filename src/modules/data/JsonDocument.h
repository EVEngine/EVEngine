#pragma once

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>

namespace eve {
namespace data {

/** @brief Thin RAII wrapper over a Poco JSON value (object/array/scalar). */
class JsonDocument {
public:
    /** @brief Creates an empty document. */
    JsonDocument();
    /** @brief Takes ownership of an existing Poco JSON root value. */
    explicit JsonDocument(Poco::Dynamic::Var root);
    JsonDocument(JsonDocument&&) noexcept;
    JsonDocument& operator=(JsonDocument&&) noexcept;
    JsonDocument(const JsonDocument&) = delete;
    ~JsonDocument() = default;

    /** @brief True when the root value is empty. */
    bool empty() const;
    /** @brief Root type predicates. */
    bool isObject() const;
    bool isArray() const;

    /** @brief Underlying Poco dynamic value. */
    Poco::Dynamic::Var&       root();
    const Poco::Dynamic::Var& root() const;

    /** @brief Root as JSON object/array (may be null when the type differs). */
    Poco::JSON::Object::Ptr object();
    Poco::JSON::Array::Ptr  array();

private:
    Poco::Dynamic::Var root_;
};

}  // namespace data
}  // namespace eve
