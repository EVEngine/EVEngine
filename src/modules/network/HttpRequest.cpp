// eve-hot-path: HTTP completion polling; network I/O runs in owned jobs.
#include "network/HttpRequest.h"
#include "data/ByteData.h"
#include "network/Network.h"
#include "network/detail/HttpRuntime.h"

#include <Poco/Exception.h>
#include <Poco/File.h>
#include <Poco/Net/Context.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/Net/HTTPSClientSession.h>
#include <Poco/Net/NetException.h>
#include <Poco/Net/RejectCertificateHandler.h>
#include <Poco/Net/SSLException.h>
#include <Poco/StreamCopier.h>
#include <Poco/Timespan.h>
#include <Poco/URI.h>
#include <openssl/ssl.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <sstream>

namespace eve::network {

// Only the owning thread restores a live event handle after checking this
// cancellation cookie. Background jobs do not retain request pointers.
struct HttpOrigin {
    explicit HttpOrigin(std::uintptr_t value) : address(value) {}
    const std::uintptr_t address;
    bool                 active = true;  // Protected by HttpRuntime::mu_.
};

namespace {

Poco::Net::Context::Ptr tlsContext(const HttpPolicy& policy) {
    try {
        using Context = Poco::Net::Context;
        Context::Ptr context =
            new Context(Context::CLIENT_USE, "", "", "",
                        policy.verifySsl ? Context::VERIFY_STRICT : Context::VERIFY_NONE, 9, false, "DEFAULT");
        context->setInvalidCertificateHandler(new Poco::Net::RejectCertificateHandler(false));
        auto* ssl = context->sslContext();
        if (SSL_CTX_set_min_proto_version(ssl, TLS1_2_VERSION) != 1)
            throw Poco::Net::SSLContextException("Cannot require TLS 1.2");
        if (!policy.verifySsl) return context;
        if (!policy.caLocation.empty()) {
            if (policy.caLocation.find('\0') != std::string::npos)
                throw Poco::Net::SSLContextException("Invalid CA location");
            const bool directory = Poco::File(policy.caLocation).isDirectory();
            if (SSL_CTX_load_verify_locations(ssl, directory ? nullptr : policy.caLocation.c_str(),
                                              directory ? policy.caLocation.c_str() : nullptr) != 1)
                throw Poco::Net::SSLContextException("Cannot load custom CA location");
        } else if (std::getenv("SSL_CERT_FILE") || std::getenv("SSL_CERT_DIR")) {
            if (SSL_CTX_set_default_verify_paths(ssl) != 1)
                throw Poco::Net::SSLContextException("Cannot load environment CA locations");
        } else {
#ifdef _WIN32
            if (SSL_CTX_load_verify_store(ssl, "org.openssl.winstore:") != 1)
                throw Poco::Net::SSLContextException("Cannot load Windows certificate roots");
#else
            // The bundled OpenSSL prefix does not supply a root bundle.
            bool loaded = false;
            for (const char* file :
                 {"/etc/ssl/certs/ca-certificates.crt", "/etc/pki/tls/certs/ca-bundle.crt", "/etc/ssl/cert.pem"}) {
                if (Poco::File(file).exists()) {
                    if (SSL_CTX_load_verify_locations(ssl, file, nullptr) != 1)
                        throw Poco::Net::SSLContextException("Cannot load system certificate roots");
                    loaded = true;
                    break;
                }
            }
            if (!loaded && SSL_CTX_set_default_verify_paths(ssl) != 1)
                throw Poco::Net::SSLContextException("Cannot load default certificate roots");
#endif
        }
        return context;
    } catch (const Poco::Net::SSLException&) {
        throw;
    } catch (const Poco::Exception& ex) {
        throw Poco::Net::SSLContextException("Cannot initialize HTTPS trust", ex.displayText());
    }
}

HttpResult performHttp(const HttpOptions& options) {
    HttpResult result;
    auto&      c = result.completion;
    c.kind       = NetKind::Http;
    c.type       = NetEvType::Err;
    try {
        Poco::URI uri(options.url);
        if ((uri.getScheme() != "http" && uri.getScheme() != "https") || uri.getHost().empty())
            throw Poco::InvalidArgumentException("Expected an absolute HTTP(S) URL");
        std::unique_ptr<Poco::Net::HTTPClientSession> session;
        if (uri.getScheme() == "https")
            session = std::make_unique<Poco::Net::HTTPSClientSession>(uri.getHost(), uri.getPort(),
                                                                      tlsContext(options.policy));
        else
            session = std::make_unique<Poco::Net::HTTPClientSession>(uri.getHost(), uri.getPort());
        session->setTimeout(
            Poco::Timespan(static_cast<Poco::Timespan::TimeDiff>((std::max)(1, options.policy.timeoutMs)) * 1000));
        std::string path = uri.getPathAndQuery();
        if (path.empty()) path = "/";
        Poco::Net::HTTPRequest request(options.method, path, Poco::Net::HTTPMessage::HTTP_1_1);
        request.setHost(uri.getHost(), uri.getPort());
        for (const auto& [key, value] : options.headers) request.set(key, value);
        if (options.body) request.setContentLength(static_cast<std::streamsize>(options.body->size()));
        auto& output = session->sendRequest(request);
        if (options.body && !options.body->empty()) {
            output.write(options.body->data(), static_cast<std::streamsize>(options.body->size()));
            if (!output) throw Poco::IOException("Cannot send HTTP body");
            result.sentBytes = options.body->size();
        }
        Poco::Net::HTTPResponse response;
        auto&                   input = session->receiveResponse(response);
        std::ostringstream      body;
        const auto              copied = Poco::StreamCopier::copyStream(input, body);
        if (input.bad() || !body) throw Poco::IOException("Cannot read HTTP response body");
        if (options.method != Poco::Net::HTTPRequest::HTTP_HEAD &&
            response.getStatus() != Poco::Net::HTTPResponse::HTTP_NO_CONTENT &&
            response.getStatus() != Poco::Net::HTTPResponse::HTTP_NOT_MODIFIED &&
            !response.getChunkedTransferEncoding() && response.hasContentLength() &&
            copied != response.getContentLength64())
            throw Poco::IOException("Truncated HTTP response body");
        const auto bytes = body.str();
        c.type           = NetEvType::HttpResp;
        c.status         = static_cast<int>(response.getStatus());
        c.bytes          = std::make_shared<std::vector<char>>(bytes.begin(), bytes.end());
    } catch (const Poco::Net::SSLException&) {
        c.reason = "tls";
    } catch (const Poco::TimeoutException&) {
        c.reason = "timeout";
    } catch (const Poco::Net::DNSException&) {
        c.reason = "dns";
    } catch (...) {
        c.reason = "refused";
    }
    return result;
}

}  // namespace

HttpRuntime::~HttpRuntime() { close(); }
HttpPolicy HttpRuntime::policy() const {
    std::lock_guard lock(mu_);
    return policy_;
}
void HttpRuntime::setTimeout(int ms) {
    std::lock_guard lock(mu_);
    policy_.timeoutMs = ms;
}
int HttpRuntime::timeout() const {
    std::lock_guard lock(mu_);
    return policy_.timeoutMs;
}
void HttpRuntime::setVerifySsl(bool verify) {
    std::lock_guard lock(mu_);
    policy_.verifySsl = verify;
}
bool HttpRuntime::getVerifySsl() const {
    std::lock_guard lock(mu_);
    return policy_.verifySsl;
}
void HttpRuntime::setCaLocation(std::string location) {
    std::lock_guard lock(mu_);
    policy_.caLocation = std::move(location);
}
bool HttpRuntime::isClosed() const {
    std::lock_guard lock(mu_);
    return closed_;
}

HttpRuntime::SubmitStatus HttpRuntime::submit(void* origin, HttpOptions options) {
    std::lock_guard lock(mu_);
    if (closed_) return SubmitStatus::Closed;
    try {
        jobs_.reserve(jobs_.size() + 1);
        const auto address  = reinterpret_cast<std::uintptr_t>(origin);
        auto&      lifetime = origins_[address];
        if (!lifetime) lifetime = std::make_shared<HttpOrigin>(address);
        auto job = std::async(std::launch::async, [snapshot = std::move(options)] { return performHttp(snapshot); });
        jobs_.push_back({lifetime, std::move(job)});
        return SubmitStatus::Accepted;
    } catch (...) {
        return SubmitStatus::Unavailable;
    }
}

HttpRuntime::PollStatus HttpRuntime::take(void* origin, HttpResult& result) {
    std::lock_guard lock(mu_);
    if (closed_) return PollStatus::Closed;
    for (auto job = jobs_.begin(); job != jobs_.end(); ++job) {
        if (job->lifetime->address == reinterpret_cast<std::uintptr_t>(origin) && job->lifetime->active &&
            job->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            result                   = job->future.get();
            result.origin_           = job->lifetime;
            result.completion.handle = origin;
            jobs_.erase(job);
            return PollStatus::Ready;
        }
    }
    return PollStatus::Pending;
}

void HttpRuntime::drain(std::vector<HttpResult>& out) {
    std::lock_guard lock(mu_);
    for (auto job = jobs_.begin(); job != jobs_.end();) {
        if (job->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++job;
            continue;
        }
        auto result = job->future.get();
        if (job->lifetime->active && !closed_) {
            result.origin_ = job->lifetime;
            out.push_back(std::move(result));
        }
        job = jobs_.erase(job);
    }
}

HttpRuntime::DeliveryStatus HttpRuntime::prepareDelivery(HttpResult& result) {
    std::lock_guard lock(mu_);
    if (closed_ || !result.origin_ || !result.origin_->active) return DeliveryStatus::Retired;
    result.completion.handle = reinterpret_cast<void*>(result.origin_->address);
    return DeliveryStatus::Ready;
}

void HttpRuntime::cancel(void* origin) {
    std::lock_guard lock(mu_);
    auto            it = origins_.find(reinterpret_cast<std::uintptr_t>(origin));
    if (it != origins_.end()) {
        it->second->active = false;
        origins_.erase(it);
    }
}

void HttpRuntime::close() {
    std::vector<Job> pending;
    {
        std::lock_guard lock(mu_);
        closed_ = true;
        for (auto& [origin, lifetime] : origins_) {
            (void)origin;
            lifetime->active = false;
        }
        origins_.clear();
        pending.swap(jobs_);
    }
    // Future destruction joins outside the lock. Each job owns its SSL session.
}

HttpRequest::HttpRequest(Network* net, std::string method, std::string url)
    : net_(net),
      runtime_(net ? net->http_ : std::weak_ptr<HttpRuntime>{}),
      method_(std::move(method)),
      url_(std::move(url)) {}
HttpRequest::~HttpRequest() {
    if (auto runtime = runtime_.lock()) runtime->cancel(this);
}
HttpRequest::HttpRequest(const HttpRequest& other)
    : net_(other.net_),
      runtime_(other.runtime_),
      method_(other.method_),
      url_(other.url_),
      headers_(other.headers_),
      body_(other.body_),
      timeoutMs_(other.timeoutMs_),
      verifySsl_(other.verifySsl_),
      caLocation_(other.caLocation_) {}

HttpRequest& HttpRequest::operator=(const HttpRequest& other) {
    if (this == &other) return *this;
    if (auto runtime = runtime_.lock()) runtime->cancel(this);
    net_        = other.net_;
    runtime_    = other.runtime_;
    method_     = other.method_;
    url_        = other.url_;
    headers_    = other.headers_;
    body_       = other.body_;
    timeoutMs_  = other.timeoutMs_;
    verifySsl_  = other.verifySsl_;
    caLocation_ = other.caLocation_;
    return *this;
}

Network* HttpRequest::network() const {
    const auto runtime = runtime_.lock();
    return runtime && !runtime->isClosed() ? net_ : nullptr;
}
void HttpRequest::setHeader(std::string key, std::string value) { headers_[std::move(key)] = std::move(value); }
void HttpRequest::setBody(eve::data::ByteData* data) {
    if (!data || data->getSize() == 0) {
        body_.reset();
        return;
    }
    const auto* begin = static_cast<const char*>(data->getData());
    body_             = std::make_shared<std::vector<char>>(begin, begin + data->getSize());
}
void HttpRequest::setBodyString(std::string body) {
    body_ = std::make_shared<std::vector<char>>(body.begin(), body.end());
}
void HttpRequest::setTimeout(int ms) { timeoutMs_ = ms; }
void HttpRequest::setVerifySsl(bool verify) { verifySsl_ = verify ? 1 : 0; }
void HttpRequest::setCaLocation(std::string location) { caLocation_ = std::move(location); }

bool HttpRequest::submit() {
    const auto runtime = runtime_.lock();
    if (!runtime) return false;
    HttpOptions options{method_, url_, headers_, body_, runtime->policy()};
    if (timeoutMs_ >= 0) options.policy.timeoutMs = timeoutMs_;
    if (verifySsl_ >= 0) options.policy.verifySsl = verifySsl_ != 0;
    if (caLocation_) options.policy.caLocation = *caLocation_;
    return runtime->submit(this, std::move(options)) == HttpRuntime::SubmitStatus::Accepted;
}

}  // namespace eve::network
