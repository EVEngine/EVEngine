#include "GenerationClient.h"
#include <Poco/Exception.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/SHA2Engine.h>
#include <Poco/URI.h>
#include <Poco/UUIDGenerator.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace ssui {
namespace {
using Object = Poco::JSON::Object;
using Array  = Poco::JSON::Array;
using namespace std::chrono_literals;
using Code = eve::DiagnosticCode;
struct Failure : std::runtime_error {
    Code code;
    Failure(Code c, const std::string& text) : std::runtime_error(text), code(c) {}
};
template <class F>
auto checked(F&& fn) -> eve::Result<decltype(fn())> {
    using T = decltype(fn());
    try {
        if constexpr (std::is_void_v<T>) {
            fn();
            return eve::Result<void>::success();
        } else
            return eve::Result<T>::success(fn());
    } catch (const Failure& e) {
        return eve::Result<T>::failure(eve::Diagnostic::error(e.code, e.what(), "ssui"));
    } catch (const Poco::Exception& e) {
        return eve::Result<T>::failure(eve::Diagnostic::error(Code::ParseError, e.displayText(), "ssui"));
    } catch (const std::exception& e) {
        return eve::Result<T>::failure(eve::Diagnostic::error(Code::Failed, e.what(), "ssui"));
    }
}
Object::Ptr object(const std::string& text) {
    Poco::JSON::Parser parser;
    parser.setDepth(64);
    auto value  = parser.parse(text);
    auto result = value.extract<Object::Ptr>();
    if (!result) throw Failure(Code::ParseError, "Expected JSON object");
    return result;
}
std::string json(Object::Ptr value) {
    std::ostringstream out;
    value->stringify(out);
    return out.str();
}
std::string uuid() { return Poco::UUIDGenerator::defaultGenerator().createRandom().toString(); }
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read material document");
    if (std::filesystem::file_size(path) > 1024 * 1024)
        throw Failure(Code::InvalidArgument, "Material document exceeds 1 MiB");
    return std::string(std::istreambuf_iterator<char>(in), {});
}
void write(const std::filesystem::path& path, const std::string& data) {
    auto temp = path;
    temp += "." + uuid() + ".tmp";
    try {
        {
            std::ofstream out(temp, std::ios::binary);
            out.write(data.data(), data.size());
            out.close();
            if (!out) throw std::runtime_error("Cannot write generated asset");
        }
#if defined(_WIN32)
        if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::runtime_error("Cannot replace generated file");
        }
#else
        std::filesystem::rename(temp, path);
#endif
    } catch (...) {
        std::error_code cleanupError;
        std::filesystem::remove(temp, cleanupError);
        throw;
    }
}
void validateAsset(const std::string& path) {
    if (path.empty()) return;
    if (path.size() != 78 || path.substr(0, 10) != "generated/" || path.substr(74) != ".png" ||
        path.substr(10, 64).find_first_not_of("0123456789abcdef") != std::string::npos)
        throw Failure(Code::InvalidArgument, "Invalid material asset path");
}
struct Material {
    int                      revision = 0;
    std::string              applied;
    std::vector<std::string> history;
};
Material restore(const std::filesystem::path& path) {
    Material result;
    if (!std::filesystem::exists(path)) return result;
    auto doc = object(read(path));
    // Unversioned prototype documents are v0; v1 adds an explicit envelope.
    if (!doc->get("revision").isInteger() || !doc->get("baseColor").isString())
        throw Failure(Code::InvalidArgument, "Invalid material field type");
    if (doc->has("schema") || doc->has("version")) {
        if (!doc->get("schema").isString() || !doc->get("version").isInteger())
            throw Failure(Code::UnknownVersion, "Invalid material envelope");
        if (doc->getValue<std::string>("schema") != "eve.ssui-material" || doc->getValue<int>("version") != 1)
            throw Failure(Code::UnknownVersion, "Unsupported material schema/version");
    }
    for (const auto& field : *doc) {
        if (field.first != "schema" && field.first != "version" && field.first != "revision" &&
            field.first != "baseColor" && field.first != "history")
            throw Failure(Code::InvalidArgument, "Unknown material field: " + field.first);
    }
    result.revision = doc->getValue<int>("revision");
    if (result.revision < 0 || result.revision == std::numeric_limits<int>::max())
        throw Failure(Code::InvalidArgument, "Invalid material revision");
    result.applied = doc->getValue<std::string>("baseColor");
    validateAsset(result.applied);
    auto history = doc->getArray("history");
    if (!history || history->size() > 4096) throw Failure(Code::InvalidArgument, "Invalid history");
    for (size_t i = 0; i < history->size(); ++i) {
        if (!history->get(i).isString()) throw Failure(Code::InvalidArgument, "Invalid history item");
        auto item = history->getElement<std::string>(i);
        validateAsset(item);
        result.history.push_back(std::move(item));
    }
    return result;
}

}  // namespace
GenerationClient::~GenerationClient() { shutdown(); }
void GenerationClient::shutdown() {
    stop_ = true;
    if (worker_.joinable()) worker_.join();
}
void        GenerationClient::requestCancel() { stop_ = true; }
bool        GenerationClient::busy() const { return busy_; }
std::string GenerationClient::status() const {
    std::lock_guard lock(mutex_);
    return stop_ ? (busy_ ? "Cancelling" : "Cancelled") : status_;
}
int GenerationClient::progress() const {
    std::lock_guard lock(mutex_);
    return progress_;
}
std::string GenerationClient::candidate() const {
    std::lock_guard lock(mutex_);
    return stop_ ? "" : candidate_;
}
std::string GenerationClient::applied() const {
    std::lock_guard lock(mutex_);
    return applied_;
}
void GenerationClient::configureImpl(const std::string& server, const std::string& project) {
    if (busy_) throw Failure(Code::Conflict, "Cancel and wait before reconfiguring");
    if (worker_.joinable()) worker_.join();
    Poco::URI uri(server);
    if (uri.getScheme() != "http" || uri.getHost().empty() || !uri.getUserInfo().empty() || !uri.getQuery().empty() ||
        !uri.getFragment().empty())
        throw Failure(Code::InvalidArgument, "This prototype requires an HTTP service URL (no credentials/query)");
    const auto  root       = std::filesystem::absolute(project);
    auto        restored   = restore(root / "generated/material.json");
    auto        clientId   = "eve-" + uuid();
    auto        serverCopy = server;
    auto        rootCopy   = root.string();
    std::string nextStatus = "Ready";
    std::filesystem::create_directories(root / "generated");
    std::lock_guard lock(mutex_);
    server_.swap(serverCopy);
    root_.swap(rootCopy);
    clientId_ = std::move(clientId);
    applied_  = std::move(restored.applied);
    history_  = std::move(restored.history);
    revision_ = restored.revision;
    candidate_.clear();
    progress_         = 0;
    completionStatus_ = eve::Status::success();
    stop_             = false;
    status_.swap(nextStatus);
}

std::string GenerationClient::request(const std::string& method, const std::string& path, const std::string& body,
                                      size_t limit) {
    Poco::URI origin(server_), uri(origin);
    uri.resolve(path);
    if (uri.getScheme() != origin.getScheme() || uri.getHost() != origin.getHost() ||
        uri.getPort() != origin.getPort() || !uri.getUserInfo().empty())
        throw Failure(Code::InvalidArgument, "Cross-origin artifact URL rejected");
    Poco::Net::HTTPClientSession session(uri.getHost(), uri.getPort());
    session.setTimeout(Poco::Timespan(5, 0));
    Poco::Net::HTTPRequest req(method, uri.getPathAndQuery(), "HTTP/1.1");
    if (method == "POST") {
        req.setContentType("application/json");
        req.setContentLength(body.size());
    }
    auto& out = session.sendRequest(req);
    out.write(body.data(), body.size());
    Poco::Net::HTTPResponse response;
    auto&                   in = session.receiveResponse(response);
    if (response.getStatus() < 200 || response.getStatus() >= 300)
        throw std::runtime_error("HTTP " + std::to_string(response.getStatus()));
    std::string content;
    char        buffer[8192];
    while (in) {
        in.read(buffer, sizeof(buffer));
        content.append(buffer, static_cast<size_t>(in.gcount()));
        if (content.size() > limit) throw std::runtime_error("Response exceeds size limit");
    }
    if (in.bad()) throw std::runtime_error("Incomplete HTTP response");
    return content;
}
std::string GenerationClient::startImpl(const std::string& recipe, const std::string& inputs,
                                        const std::string& context) {
    if (busy_) throw Failure(Code::Conflict, "Generation already running");
    if (inputs.size() > 1024 * 1024 || context.size() > 1024 * 1024)
        throw Failure(Code::InvalidArgument, "JSON inputs exceed 1 MiB");
    object(inputs);
    object(context);
    auto requestId = uuid();
    if (server_.empty()) throw Failure(Code::PreconditionViolation, "Configure first");
    if (worker_.joinable()) worker_.join();
    {
        std::lock_guard lock(mutex_);
        candidate_.clear();
        completionStatus_ = eve::Status::success();
        status_           = "Connecting";
        progress_         = 0;
    }
    stop_ = false;
    busy_ = true;
    try {
        worker_ = std::thread(&GenerationClient::run, this, recipe, inputs, context, revision_, requestId);
    } catch (...) {
        busy_ = false;
        throw;
    }
    return requestId;
}
void GenerationClient::run(std::string recipe, std::string inputs, std::string context, int revision,
                           std::string requestId) {
    std::string jobPath;
    try {
        auto caps = object(request("GET", "/api/generation/v1/capabilities"));
        if (caps->getValue<std::string>("protocol") != "ssui-generation" ||
            caps->getValue<std::string>("version") != "1")
            throw Failure(Code::Unsupported, "Unsupported protocol");
        auto recipes = object(request("GET", "/api/generation/v1/recipes"))->getArray("items");
        if (!recipes) throw Failure(Code::ParseError, "Missing recipes");
        bool found = false;
        for (size_t i = 0; i < recipes->size(); ++i) {
            auto entry = recipes->getObject(i);
            if (!entry) throw Failure(Code::ParseError, "Invalid recipe entry");
            if (entry->getValue<std::string>("id") == recipe) found = true;
        }
        if (!found) throw Failure(Code::NotFound, "Recipe unavailable");
        if (stop_) throw std::runtime_error("Cancelled");
        Object::Ptr payload = new Object;
        payload->set("recipe", recipe);
        payload->set("inputs", object(inputs));
        payload->set("context", object(context));
        payload->set("clientId", clientId_);
        payload->set("idempotencyKey", requestId);
        auto        job = object(request("POST", "/api/generation/v1/jobs", json(payload)));
        std::string encoded;
        Poco::URI::encode(job->getValue<std::string>("jobId"), "/?#", encoded);
        jobPath       = "/api/generation/v1/jobs/" + encoded;
        auto deadline = std::chrono::steady_clock::now() + 120s;
        while (true) {
            if (stop_ || std::chrono::steady_clock::now() > deadline) {
                request("POST", jobPath + "/cancel", "{}");
                throw std::runtime_error(stop_ ? "Cancelled" : "Generation timed out");
            }
            auto state = job->getValue<std::string>("state");
            if (state != "queued" && state != "running" && state != "succeeded" && state != "failed" &&
                state != "cancelled")
                throw Failure(Code::ParseError, "Unknown job state");
            {
                std::lock_guard lock(mutex_);
                status_   = state;
                progress_ = job->getValue<int>("progress");
            }
            if (state == "succeeded") break;
            if (state == "failed" || state == "cancelled")
                throw Failure(state == "cancelled" ? Code::Cancelled : Code::Failed, "Job " + state);
            std::this_thread::sleep_for(200ms);
            job = object(request("GET", jobPath));
        }
        auto artifacts = job->getArray("artifacts");
        if (!artifacts || artifacts->empty()) throw Failure(Code::NotFound, "No artifacts");
        auto artifact = artifacts->getObject(0);
        if (!artifact) throw Failure(Code::ParseError, "Invalid artifact");
        if (artifact->getValue<std::string>("kind") != "image" ||
            artifact->getValue<std::string>("mimeType") != "image/png")
            throw Failure(Code::Unsupported, "This editor accepts PNG only");
        auto             bytes = request("GET", artifact->getValue<std::string>("url"), "", 16 * 1024 * 1024);
        Poco::SHA2Engine sha;
        sha.update(bytes);
        auto digest = Poco::DigestEngine::digestToHex(sha.digest());
        if (digest != artifact->getValue<std::string>("sha256") ||
            bytes.compare(0, 8, std::string("\x89PNG\r\n\x1a\n", 8)) != 0)
            throw Failure(Code::HashMismatch, "PNG checksum or signature mismatch");
        if (stop_) throw std::runtime_error("Cancelled");
        auto relative = "generated/" + digest + ".png";
        write(std::filesystem::path(root_) / relative, bytes);
        {
            std::lock_guard lock(mutex_);
            if (!stop_) {
                candidate_         = relative;
                candidateRevision_ = revision;
                status_            = "Candidate ready";
                progress_          = 100;
            } else
                status_ = "Cancelled";
        }
    } catch (const Failure& e) {
        std::lock_guard lock(mutex_);
        status_           = "Error: " + std::string(e.what());
        completionStatus_ = eve::Status::failure(eve::Diagnostic::error(e.code, status_, "generation"));
    } catch (const Poco::Exception& e) {
        std::lock_guard lock(mutex_);
        status_           = "Error: " + e.displayText();
        completionStatus_ = eve::Status::failure(eve::Diagnostic::error(Code::Failed, status_, "generation"));
    } catch (const std::exception& e) {
        std::lock_guard lock(mutex_);
        status_           = "Error: " + std::string(e.what());
        completionStatus_ = eve::Status::failure(eve::Diagnostic::error(Code::Failed, status_, "generation"));
    }
    busy_ = false;
}
void GenerationClient::checkRevision() const {
    auto path = std::filesystem::path(root_) / "generated/material.json";
    int  disk = restore(path).revision;
    if (disk != revision_) throw Failure(Code::Conflict, "Material changed; reconfigure before applying");
}
void GenerationClient::save(const std::string& path, const std::vector<std::string>& history, int revision) {
    if (history.size() > 4096 || revision == std::numeric_limits<int>::max())
        throw Failure(Code::InvalidArgument, "Material history/revision limit reached");
    Object::Ptr doc = new Object;
    doc->set("schema", "eve.ssui-material");
    doc->set("version", 1);
    doc->set("revision", revision);
    doc->set("baseColor", path);
    Array::Ptr list = new Array;
    for (auto& item : history) list->add(item);
    doc->set("history", list);
    write(std::filesystem::path(root_) / "generated/material.json", json(doc));
}
std::string GenerationClient::applyImpl() {
    if (busy_) throw Failure(Code::Conflict, "Wait for generation");
    std::lock_guard lock(mutex_);
    if (root_.empty()) throw Failure(Code::PreconditionViolation, "Configure first");
    checkRevision();
    if (stop_ || candidate_.empty() || candidateRevision_ != revision_)
        throw Failure(Code::PreconditionViolation, "No current candidate");
    auto history = history_;
    history.push_back(applied_);
    auto        next       = candidate_;
    auto        result     = next;
    std::string nextStatus = "Applied";
    save(next, history, revision_ + 1);
    applied_.swap(next);
    candidate_.clear();
    history_.swap(history);
    ++revision_;
    status_.swap(nextStatus);
    return result;
}
std::string GenerationClient::undoImpl() {
    if (busy_) throw Failure(Code::Conflict, "Wait for generation");
    std::lock_guard lock(mutex_);
    if (root_.empty()) throw Failure(Code::PreconditionViolation, "Configure first");
    checkRevision();
    if (history_.empty()) throw Failure(Code::PreconditionViolation, "Nothing to undo");
    auto history  = history_;
    auto previous = history.back();
    history.pop_back();
    auto        result     = previous;
    std::string nextStatus = "Undone";
    save(previous, history, revision_ + 1);
    applied_.swap(previous);
    history_.swap(history);
    candidate_.clear();
    ++revision_;
    status_.swap(nextStatus);
    return result;
}
eve::Result<void> GenerationClient::configure(std::string_view server, std::string_view project) {
    return checked([&] { configureImpl(std::string(server), std::string(project)); });
}
eve::Result<std::string> GenerationClient::start(std::string_view recipe, std::string_view inputs,
                                                 std::string_view context) {
    return checked([&] { return startImpl(std::string(recipe), std::string(inputs), std::string(context)); });
}
eve::Result<std::string> GenerationClient::apply() {
    return checked([&] { return applyImpl(); });
}
eve::Result<std::string> GenerationClient::undo() {
    return checked([&] { return undoImpl(); });
}
eve::Result<std::string> GenerationClient::completion() const {
    std::lock_guard lock(mutex_);
    if (busy_) return eve::Result<std::string>::failure(eve::Diagnostic::error(Code::Conflict, "Pending"));
    if (stop_) return eve::Result<std::string>::failure(eve::Diagnostic::error(Code::Cancelled, "Cancelled"));
    if (completionStatus_.isFailure()) return eve::Result<std::string>::failure(completionStatus_);
    if (candidate_.empty())
        return eve::Result<std::string>::failure(eve::Diagnostic::error(Code::NotFound, "No candidate"));
    return eve::Result<std::string>::success(candidate_);
}

}  // namespace ssui
