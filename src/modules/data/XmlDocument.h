#pragma once

#include <Poco/AutoPtr.h>
#include <Poco/DOM/Document.h>

namespace eve {
namespace data {

class XmlDocument {
public:
    XmlDocument();
    explicit XmlDocument(Poco::AutoPtr<Poco::XML::Document> doc);
    XmlDocument(XmlDocument&&) noexcept;
    XmlDocument& operator=(XmlDocument&&) noexcept;
    XmlDocument(const XmlDocument&) = delete;
    ~XmlDocument() = default;

    bool empty() const;

    Poco::XML::Document*       get();
    const Poco::XML::Document* get() const;
    Poco::XML::Document*       operator->();
    Poco::XML::Document&       operator*();

private:
    Poco::AutoPtr<Poco::XML::Document> doc_;
};

}  // namespace data
}  // namespace eve
