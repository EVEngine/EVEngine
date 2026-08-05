#include "XmlDocument.h"

namespace eve {
namespace data {

XmlDocument::XmlDocument() : doc_(new Poco::XML::Document) {}

XmlDocument::XmlDocument(Poco::AutoPtr<Poco::XML::Document> doc) : doc_(doc) {}

XmlDocument::XmlDocument(XmlDocument&&) noexcept            = default;
XmlDocument& XmlDocument::operator=(XmlDocument&&) noexcept = default;

bool XmlDocument::empty() const { return doc_.isNull(); }

Poco::XML::Document*       XmlDocument::get() { return doc_.get(); }
const Poco::XML::Document* XmlDocument::get() const { return doc_.get(); }

Poco::XML::Document* XmlDocument::operator->() { return doc_.get(); }

Poco::XML::Document& XmlDocument::operator*() { return *doc_; }

}  // namespace data
}  // namespace eve
