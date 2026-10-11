#pragma once

// Shared MCP wire helpers.
//
// MCP framing here is newline-delimited JSON (see devtools/McpServer.hpp), so
// every payload has to be a single line: stringify with indent/step 0 and escape
// raw control characters. These helpers are inline so the embedded server and
// its satellite tool modules (AgentDevelopmentMcp, McpRuntimeTools, ...) build
// identical content envelopes without a second JSON codec.

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Stringifier.h>

#include <cstdio>
#include <sstream>
#include <string>

namespace eve::dev {

/** @brief Compact single-line JSON text for a newline-framed MCP payload. */
inline std::string mcpStringify(const Poco::Dynamic::Var& value) {
    std::ostringstream out;
    Poco::JSON::Stringifier::stringify(value, out, 0, 0);
    return out.str();
}

/** @brief Escape a raw string for embedding inside a JSON string literal. */
inline std::string mcpJsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
    for (char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

/**
 * @brief MCP `tools/call` result object carrying one text content item.
 * @param text Payload text (compact JSON for structured tools).
 * @param isError Whether the client should treat the call as failed.
 */
inline std::string textContentResult(const std::string& text, bool isError = false) {
    Poco::JSON::Object::Ptr result  = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    Poco::JSON::Array::Ptr  content = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    Poco::JSON::Object::Ptr item    = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    item->set("type", "text");
    item->set("text", text);
    content->add(item);
    result->set("content", content);
    if (isError) result->set("isError", true);
    return mcpStringify(Poco::Dynamic::Var(result));
}

/**
 * @brief MCP `tools/call` result object carrying one image content item.
 * @param base64Png Base64 PNG payload without any `data:` URL prefix.
 * @param mimeType Image MIME type (`image/png`).
 * @param caption Optional text content item returned next to the image.
 */
inline std::string imageContentResult(const std::string& base64Png, const std::string& mimeType,
                                      const std::string& caption) {
    Poco::JSON::Object::Ptr result  = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    Poco::JSON::Array::Ptr  content = Poco::JSON::Array::Ptr(new Poco::JSON::Array());
    Poco::JSON::Object::Ptr image   = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
    image->set("type", "image");
    image->set("data", base64Png);
    image->set("mimeType", mimeType);
    content->add(image);
    if (!caption.empty()) {
        Poco::JSON::Object::Ptr item = Poco::JSON::Object::Ptr(new Poco::JSON::Object());
        item->set("type", "text");
        item->set("text", caption);
        content->add(item);
    }
    result->set("content", content);
    return mcpStringify(Poco::Dynamic::Var(result));
}

}  // namespace eve::dev
