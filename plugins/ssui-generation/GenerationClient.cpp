#include "GenerationClient.h"
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Net/HTTPClientSession.h>
#include <Poco/Net/HTTPRequest.h>
#include <Poco/Net/HTTPResponse.h>
#include <Poco/SHA2Engine.h>
#include <Poco/URI.h>
#include <Poco/UUIDGenerator.h>
#include <Poco/Exception.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace ssui {
namespace {
using Object = Poco::JSON::Object;
using Array = Poco::JSON::Array;
using namespace std::chrono_literals;
Object::Ptr object(const std::string& text) {
    auto value = Poco::JSON::Parser().parse(text);
    return value.extract<Object::Ptr>();
}
std::string json(Object::Ptr value) { std::ostringstream out; value->stringify(out); return out.str(); }
std::string uuid() { return Poco::UUIDGenerator::defaultGenerator().createRandom().toString(); }
std::string read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot read material document");
    return std::string(std::istreambuf_iterator<char>(in), {});
}
void write(const std::filesystem::path& path, const std::string& data) {
    auto temp = path; temp += "." + uuid() + ".tmp";
    { std::ofstream out(temp, std::ios::binary); out.write(data.data(), data.size()); out.close();
      if (!out) throw std::runtime_error("Cannot write generated asset"); }
#if defined(_WIN32)
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(temp); throw std::runtime_error("Cannot replace generated file");
    }
#else
    std::filesystem::rename(temp, path);
#endif
}
void validateAsset(const std::string& path) {
    if (path.empty()) return;
    if (path.size() != 78 || path.substr(0,10) != "generated/" || path.substr(74) != ".png" ||
        path.substr(10,64).find_first_not_of("0123456789abcdef") != std::string::npos)
        throw std::runtime_error("Invalid material asset path");
}
}
GenerationClient::~GenerationClient() { shutdown(); }
void GenerationClient::shutdown() { stop_ = true; if (worker_.joinable()) worker_.join(); }
void GenerationClient::cancel() { stop_ = true; }
bool GenerationClient::busy() const { return busy_; }
std::string GenerationClient::status() const { std::lock_guard lock(mutex_); return status_; }
int GenerationClient::progress() const { std::lock_guard lock(mutex_); return progress_; }
std::string GenerationClient::candidate() const { std::lock_guard lock(mutex_); return stop_ ? "" : candidate_; }
std::string GenerationClient::applied() const { std::lock_guard lock(mutex_); return applied_; }
void GenerationClient::configure(const std::string& server, const std::string& project) {
    if (busy_) throw std::runtime_error("Cancel and wait before reconfiguring");
    shutdown();
    Poco::URI uri(server);
    if (uri.getScheme() != "http" || uri.getHost().empty() || !uri.getUserInfo().empty() || !uri.getQuery().empty() || !uri.getFragment().empty())
        throw std::runtime_error("This prototype requires an HTTP service URL (no credentials/query)");
    std::lock_guard lock(mutex_);
    server_ = server; root_ = std::filesystem::absolute(project).string(); clientId_ = "eve-" + uuid();
    std::filesystem::create_directories(std::filesystem::path(root_) / "generated");
    applied_.clear(); candidate_.clear(); history_.clear(); revision_ = 0;
    auto path = std::filesystem::path(root_) / "generated/material.json";
    if (std::filesystem::exists(path)) {
        auto doc = object(read(path)); revision_ = doc->getValue<int>("revision");
        applied_ = doc->getValue<std::string>("baseColor"); validateAsset(applied_);
        auto history = doc->getArray("history");
        for (size_t i=0; i<history->size(); ++i) { auto item=history->getElement<std::string>(i); validateAsset(item); history_.push_back(item); }
    }
    stop_ = false; status_ = "Ready";
}
std::string GenerationClient::request(const std::string& method, const std::string& path, const std::string& body, size_t limit) {
    Poco::URI origin(server_), uri(origin); uri.resolve(path);
    if (uri.getScheme()!=origin.getScheme() || uri.getHost()!=origin.getHost() || uri.getPort()!=origin.getPort() || !uri.getUserInfo().empty())
        throw std::runtime_error("Cross-origin artifact URL rejected");
    Poco::Net::HTTPClientSession session(uri.getHost(), uri.getPort());
    session.setTimeout(Poco::Timespan(5,0));
    Poco::Net::HTTPRequest req(method, uri.getPathAndQuery(), "HTTP/1.1");
    if (method=="POST") { req.setContentType("application/json"); req.setContentLength(body.size()); }
    auto& out=session.sendRequest(req); out.write(body.data(), body.size());
    Poco::Net::HTTPResponse response; auto& in=session.receiveResponse(response);
    if (response.getStatus()<200 || response.getStatus()>=300) throw std::runtime_error("HTTP " + std::to_string(response.getStatus()));
    std::string content; char buffer[8192];
    while (in) { in.read(buffer, sizeof(buffer)); content.append(buffer, static_cast<size_t>(in.gcount()));
        if (content.size()>limit) throw std::runtime_error("Response exceeds size limit"); }
    if (in.bad()) throw std::runtime_error("Incomplete HTTP response");
    return content;
}
bool GenerationClient::start(const std::string& recipe, const std::string& inputs, const std::string& context) {
    if (busy_) return false;
    object(inputs); object(context);
    if (server_.empty()) throw std::runtime_error("Configure first");
    if (worker_.joinable()) worker_.join();
    { std::lock_guard lock(mutex_); candidate_.clear(); status_="Connecting"; progress_=0; }
    stop_=false; busy_=true;
    try { worker_=std::thread(&GenerationClient::run,this,recipe,inputs,context,revision_); }
    catch (...) { busy_=false; throw; }
    return true;
}
void GenerationClient::run(std::string recipe, std::string inputs, std::string context, int revision) {
    std::string jobPath;
    try {
        auto caps=object(request("GET","/api/generation/v1/capabilities"));
        if (caps->getValue<std::string>("protocol")!="ssui-generation" || caps->getValue<std::string>("version")!="1") throw std::runtime_error("Unsupported protocol");
        auto recipes=object(request("GET","/api/generation/v1/recipes"))->getArray("items");
        bool found=false; for(size_t i=0;i<recipes->size();++i) if(recipes->getObject(i)->getValue<std::string>("id")==recipe) found=true;
        if(!found) throw std::runtime_error("Recipe unavailable");
        if(stop_) throw std::runtime_error("Cancelled");
        Object::Ptr payload=new Object; payload->set("recipe",recipe); payload->set("inputs",object(inputs)); payload->set("context",object(context));
        payload->set("clientId",clientId_); payload->set("idempotencyKey",uuid());
        auto job=object(request("POST","/api/generation/v1/jobs",json(payload)));
        std::string encoded; Poco::URI::encode(job->getValue<std::string>("jobId"),"/?#",encoded); jobPath="/api/generation/v1/jobs/"+encoded;
        auto deadline=std::chrono::steady_clock::now()+120s;
        while(true) {
            if(stop_ || std::chrono::steady_clock::now()>deadline) { request("POST",jobPath+"/cancel","{}"); throw std::runtime_error(stop_ ? "Cancelled" : "Generation timed out"); }
            auto state=job->getValue<std::string>("state");
            { std::lock_guard lock(mutex_); status_=state; progress_=job->getValue<int>("progress"); }
            if(state=="succeeded") break;
            if(state=="failed" || state=="cancelled") throw std::runtime_error("Job "+state);
            std::this_thread::sleep_for(200ms); job=object(request("GET",jobPath));
        }
        auto artifacts=job->getArray("artifacts"); if(artifacts->empty()) throw std::runtime_error("No artifacts");
        auto artifact=artifacts->getObject(0);
        if(artifact->getValue<std::string>("kind")!="image" || artifact->getValue<std::string>("mimeType")!="image/png") throw std::runtime_error("This editor accepts PNG only");
        auto bytes=request("GET",artifact->getValue<std::string>("url"),"",16*1024*1024);
        Poco::SHA2Engine sha; sha.update(bytes); auto digest=Poco::DigestEngine::digestToHex(sha.digest());
        if(digest!=artifact->getValue<std::string>("sha256") || bytes.compare(0,8,std::string("\x89PNG\r\n\x1a\n",8))!=0) throw std::runtime_error("PNG checksum or signature mismatch");
        if(stop_) throw std::runtime_error("Cancelled");
        auto relative="generated/"+digest+".png"; write(std::filesystem::path(root_)/relative,bytes);
        { std::lock_guard lock(mutex_); if(!stop_) { candidate_=relative; candidateRevision_=revision; status_="Candidate ready"; progress_=100; } else status_="Cancelled"; }
    } catch(const Poco::Exception& e) { std::lock_guard lock(mutex_); status_="Error: "+e.displayText(); }
      catch(const std::exception& e) { std::lock_guard lock(mutex_); status_="Error: "+std::string(e.what()); }
    busy_=false;
}
void GenerationClient::checkRevision() const {
    auto path=std::filesystem::path(root_)/"generated/material.json";
    int disk=std::filesystem::exists(path) ? object(read(path))->getValue<int>("revision") : 0;
    if(disk!=revision_) throw std::runtime_error("Material changed; reconfigure before applying");
}
void GenerationClient::save(const std::string& path,const std::vector<std::string>& history,int revision) {
    Object::Ptr doc=new Object; doc->set("revision",revision); doc->set("baseColor",path);
    Array::Ptr list=new Array; for(auto& item:history) list->add(item); doc->set("history",list);
    write(std::filesystem::path(root_)/"generated/material.json",json(doc));
}
std::string GenerationClient::apply() {
    if(busy_) throw std::runtime_error("Wait for generation");
    std::lock_guard lock(mutex_); checkRevision();
    if(stop_ || candidate_.empty() || candidateRevision_!=revision_) throw std::runtime_error("No current candidate");
    auto history=history_; history.push_back(applied_); save(candidate_,history,revision_+1);
    applied_=candidate_; candidate_.clear(); history_=history; ++revision_; status_="Applied"; return applied_;
}
std::string GenerationClient::undo() {
    if(busy_) throw std::runtime_error("Wait for generation");
    std::lock_guard lock(mutex_); checkRevision(); if(history_.empty()) throw std::runtime_error("Nothing to undo");
    auto history=history_; auto previous=history.back(); history.pop_back(); save(previous,history,revision_+1);
    applied_=previous; history_=history; candidate_.clear(); ++revision_; status_="Undone"; return applied_;
}
}
