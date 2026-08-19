#pragma once

#include <Poco/AutoPtr.h>
#include <Poco/DOM/Document.h>

namespace eve {
namespace data {

/** @brief Thin RAII wrapper over a Poco XML DOM document. */
class XmlDocument {
public:
    /** @brief Creates an empty document. */
    XmlDocument();
    /** @brief Takes ownership of an existing Poco DOM document. */
    explicit XmlDocument(Poco::AutoPtr<Poco::XML::Document> doc);
    XmlDocument(XmlDocument&&) noexcept;
    XmlDocument& operator=(XmlDocument&&) noexcept;
    XmlDocument(const XmlDocument&) = delete;
    ~XmlDocument() = default;

    /** @brief True when no underlying document is held. */
    bool empty() const;

    /** @brief Underlying Poco document access. */
    Poco::XML::Document*       get();
    const Poco::XML::Document* get() const;
    /** @brief Convenience pointer/ref access. */
    Poco::XML::Document*       operator->();
    Poco::XML::Document&       operator*();

private:
    Poco::AutoPtr<Poco::XML::Document> doc_;
};

}  // namespace data
}  // namespace eve
