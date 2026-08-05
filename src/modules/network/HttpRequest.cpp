#include "network/HttpRequest.h"
#include "network/Network.h"
#include "network/NetWorker.h"
#include "data/ByteData.h"

#include <Poco/URI.h>
#include <Poco/Timespan.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/StreamCopier.h>
#include <Poco/Exception.h>

#include <sstream>
#include <thread>

namespace eve::network {

HttpRequest::HttpRequest(Network* net, std::string method, std::string url)
    : net_(net), method_(std::move(method)), url_(std::move(url)) {}

void HttpRequest::setHeader(std::string k, std::string v) {
    headers_[std::move(k)] = std::move(v);
}

void HttpRequest::setBody(eve::data::ByteData* data) {
    if (!data) {
        body_.reset();
        return;
    }
    body_ = std::make_shared<std::vector<char>>(
        static_cast<char*>(data->getData()),
        static_cast<char*>(data->getData()) + data->getSize());
}

void HttpRequest::setBodyString(std::string s) {
    body_ = std::make_shared<std::vector<char>>(s.begin(), s.end());
}

void HttpRequest::setTimeout(int ms) {
    timeoutMs_ = ms;
}

void HttpRequest::setVerifySsl(bool verify) {
    verifySsl_ = verify ? 1 : 0;
}

bool HttpRequest::submit() {
    if (!net_) return false;
    auto* self = this;
    auto method = method_;
    auto url = url_;
    auto headers = headers_;
    auto body = body_;
    int timeoutMs = timeoutMs_ >= 0 ? timeoutMs_ : net_->getTimeout();
    (void)verifySsl_;

    // Dedicated thread: HTTP would block NetWorker and prevent accept/poll.
    std::thread([self, method, url, headers, body, timeoutMs]() {
        NetCompletion c;
        c.kind   = NetKind::Http;
        c.handle = self;
        try {
            Poco::URI uri(url);
            if (uri.getScheme() == "https") {
                c.type   = NetEvType::Err;
                c.reason = "tls";
                self->net_->post(std::move(c));
                return;
            }
            Poco::Net::HTTPClientSession session(uri.getHost(), uri.getPort());
            session.setTimeout(Poco::Timespan(timeoutMs / 1000, (timeoutMs % 1000) * 1000));
            std::string path = uri.getPathAndQuery();
            if (path.empty()) path = "/";
            Poco::Net::HTTPRequest req(method, path, Poco::Net::HTTPMessage::HTTP_1_1);
            req.setHost(uri.getHost());
            for (auto& kv : headers) req.set(kv.first, kv.second);
            if (body && !body->empty()) {
                req.setContentLength(static_cast<std::streamsize>(body->size()));
            }
            std::ostream& os = session.sendRequest(req);
            if (body && !body->empty()) {
                os.write(body->data(), static_cast<std::streamsize>(body->size()));
            }
            Poco::Net::HTTPResponse resp;
            std::istream& rs = session.receiveResponse(resp);
            std::ostringstream oss;
            Poco::StreamCopier::copyStream(rs, oss);
            std::string content = oss.str();
            c.type   = NetEvType::HttpResp;
            c.status = static_cast<int>(resp.getStatus());
            c.bytes  = std::make_shared<std::vector<char>>(content.begin(), content.end());
            self->net_->post(std::move(c));
        } catch (const Poco::Exception& ex) {
            c.type = NetEvType::Err;
            std::string msg = ex.displayText();
            if (msg.find("timed") != std::string::npos || msg.find("Timeout") != std::string::npos)
                c.reason = "timeout";
            else if (msg.find("host") != std::string::npos || msg.find("DNS") != std::string::npos)
                c.reason = "dns";
            else
                c.reason = "refused";
            self->net_->post(std::move(c));
        } catch (...) {
            c.type   = NetEvType::Err;
            c.reason = "refused";
            self->net_->post(std::move(c));
        }
    }).detach();
    return true;
}

}  // namespace eve::network
