#include "cmdline.h"

#include <CLI11.hpp>

#include <iostream>
#include <string>

#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS) && !defined(EVENGINE_WEBGPU)
#include "scripts.h"

#include "common/Runtime.h"
#include "common/ScriptCompiler.h"
#include "common/ScriptModule.h"

#include <Poco/Dynamic/Var.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <unordered_map>
#endif

namespace eve::cmd {
#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS) && !defined(EVENGINE_WEBGPU)
namespace {

std::string stringify(const Poco::Dynamic::Var& value) {
    std::ostringstream output;
    Poco::JSON::Stringifier::stringify(value, output);
    return output.str();
}

void writeFrame(const Poco::JSON::Object::Ptr& message) {
    const std::string body = stringify(Poco::Dynamic::Var(message));
    std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body << std::flush;
}

bool readFrame(std::string& body) {
    std::string line;
    size_t      length = 0;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        constexpr std::string_view prefix = "Content-Length:";
        if (line.rfind(prefix, 0) == 0) {
            try {
                length = static_cast<size_t>(std::stoull(line.substr(prefix.size())));
            } catch (const std::exception&) {
                return false;
            }
        }
    }
    if (length == 0 || !std::cin) return false;
    body.resize(length);
    std::cin.read(body.data(), static_cast<std::streamsize>(length));
    return static_cast<size_t>(std::cin.gcount()) == length;
}

class ProjectDirectoryProvider final : public script::IScriptModuleProvider {
public:
    explicit ProjectDirectoryProvider(std::filesystem::path root)
        : root_(std::filesystem::weakly_canonical(std::move(root))) {}

    script::ScriptModuleStatus resolve(const script::ScriptModuleRequest& request, std::string& canonicalUri,
                                       std::string& error) override {
        return script::ScriptModuleResolver::canonicalize(request, canonicalUri, error)
                   ? script::ScriptModuleStatus::Found
                   : script::ScriptModuleStatus::Error;
    }

    script::ScriptModuleStatus load(std::string_view canonicalUri, script::ScriptModuleSource& source,
                                    std::string& error) override {
        constexpr std::string_view prefix = "game:/";
        if (canonicalUri.rfind(prefix, 0) != 0) return script::ScriptModuleStatus::NotHandled;
        const auto path = std::filesystem::weakly_canonical(root_ / std::string(canonicalUri.substr(prefix.size())));
        const auto relative = path.lexically_relative(root_).generic_string();
        if (relative.empty() || relative.rfind("..", 0) == 0) {
            error = "module escapes the project root: " + std::string(canonicalUri);
            return script::ScriptModuleStatus::Error;
        }
        std::ifstream input(path, std::ios::binary);
        if (!input) return script::ScriptModuleStatus::NotHandled;
        source.canonicalUri = std::string(canonicalUri);
        source.utf8Source.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        source.debugOrigin = path.string();
        return script::ScriptModuleStatus::Found;
    }

private:
    std::filesystem::path root_;
};

Poco::JSON::Object::Ptr response(const Poco::Dynamic::Var& id, const Poco::Dynamic::Var& result) {
    Poco::JSON::Object::Ptr message = new Poco::JSON::Object();
    message->set("jsonrpc", "2.0");
    message->set("id", id);
    message->set("result", result);
    return message;
}

Poco::JSON::Object::Ptr errorResponse(const Poco::Dynamic::Var& id, int code, std::string messageText) {
    Poco::JSON::Object::Ptr error = new Poco::JSON::Object();
    error->set("code", code);
    error->set("message", std::move(messageText));
    Poco::JSON::Object::Ptr message = new Poco::JSON::Object();
    message->set("jsonrpc", "2.0");
    message->set("id", id);
    message->set("error", error);
    return message;
}

std::string documentUri(const Poco::JSON::Object::Ptr& params) {
    if (!params) return {};
    auto document = params->getObject("textDocument");
    return document ? document->optValue<std::string>("uri", "") : std::string{};
}

std::string wordAt(std::string_view source, size_t line, size_t character) {
    size_t offset = 0;
    for (size_t current = 0; current < line && offset < source.size(); ++current) {
        const size_t newline = source.find('\n', offset);
        offset               = newline == std::string_view::npos ? source.size() : newline + 1;
    }
    offset = std::min(offset + character, source.size());
    size_t begin = offset;
    size_t end   = offset;
    while (begin > 0 && (std::isalnum(static_cast<unsigned char>(source[begin - 1])) || source[begin - 1] == '_'))
        --begin;
    while (end < source.size() &&
           (std::isalnum(static_cast<unsigned char>(source[end])) || source[end] == '_'))
        ++end;
    return std::string(source.substr(begin, end - begin));
}

std::vector<std::string> moduleNames(std::string_view source, const std::regex& pattern) {
    const std::string              owned(source);
    std::vector<std::string>       result;
    for (std::sregex_iterator it(owned.begin(), owned.end(), pattern), end; it != end; ++it)
        result.push_back((*it)[1].str());
    return result;
}

class LanguageServerSession {
public:
    explicit LanguageServerSession(std::string root)
        : root_(std::filesystem::absolute(std::move(root)).lexically_normal().string()),
          runtime_(2048, ssq::Libs::ALL) {
        runtime_.scriptModules().registerProvider(
            std::make_shared<ProjectDirectoryProvider>(std::filesystem::path(root_)), 100);
        const std::string contract = module_list_content ? module_list_content : "";
        const size_t      split = contract.find("eve_module_contract");
        static const std::regex slotPattern(R"(\bslot\s*=\s*["']([^"']+)["'])");
        static const std::regex namePattern(R"(\bname\s*=\s*["']([^"']+)["'])");
        activeModules_ = moduleNames(contract.substr(0, split), slotPattern);
        knownModules_ = moduleNames(split == std::string::npos ? std::string_view{} : std::string_view(contract).substr(split),
                                    namePattern);
    }

    int run() {
        std::string body;
        while (readFrame(body)) {
            Poco::JSON::Parser parser;
            Poco::JSON::Object::Ptr request;
            try {
                request = parser.parse(body).extract<Poco::JSON::Object::Ptr>();
            } catch (const std::exception& error) {
                writeFrame(errorResponse(Poco::Dynamic::Var(), -32700, error.what()));
                continue;
            }
            if (!request) continue;
            const std::string method = request->optValue<std::string>("method", "");
            const auto        params = request->getObject("params");
            const bool        hasId  = request->has("id");
            const auto        id     = hasId ? request->get("id") : Poco::Dynamic::Var();

            if (method == "initialize") {
                Poco::JSON::Object::Ptr sync = new Poco::JSON::Object();
                sync->set("openClose", true);
                sync->set("change", 1);
                Poco::JSON::Object::Ptr completion = new Poco::JSON::Object();
                completion->set("resolveProvider", false);
                Poco::JSON::Array::Ptr triggers = new Poco::JSON::Array();
                triggers->add(".");
                triggers->add(":");
                completion->set("triggerCharacters", triggers);
                Poco::JSON::Object::Ptr capabilities = new Poco::JSON::Object();
                capabilities->set("textDocumentSync", sync);
                capabilities->set("completionProvider", completion);
                capabilities->set("hoverProvider", true);
                Poco::JSON::Object::Ptr diagnosticProvider = new Poco::JSON::Object();
                diagnosticProvider->set("interFileDependencies", true);
                diagnosticProvider->set("workspaceDiagnostics", false);
                capabilities->set("diagnosticProvider", diagnosticProvider);
                Poco::JSON::Object::Ptr result = new Poco::JSON::Object();
                result->set("capabilities", capabilities);
                writeFrame(response(id, Poco::Dynamic::Var(result)));
            } else if (method == "shutdown") {
                shutdown_ = true;
                writeFrame(response(id, Poco::Dynamic::Var()));
            } else if (method == "exit") {
                return shutdown_ ? 0 : 1;
            } else if (method == "textDocument/didOpen") {
                auto document = params ? params->getObject("textDocument") : nullptr;
                if (document) update(document->optValue<std::string>("uri", ""),
                                     document->optValue<std::string>("text", ""));
            } else if (method == "textDocument/didChange") {
                const std::string uri = documentUri(params);
                auto changes = params ? params->getArray("contentChanges") : nullptr;
                if (changes && changes->size() != 0) {
                    auto change = changes->getObject(static_cast<unsigned int>(changes->size() - 1));
                    if (change) update(uri, change->optValue<std::string>("text", ""));
                }
            } else if (method == "textDocument/didClose") {
                const std::string uri = documentUri(params);
                documents_.erase(uri);
                canonical_.erase(uri);
            } else if (method == "textDocument/completion") {
                writeFrame(response(id, Poco::Dynamic::Var(completion(params))));
            } else if (method == "textDocument/hover") {
                writeFrame(response(id, hover(params)));
            } else if (method == "textDocument/diagnostic") {
                Poco::JSON::Object::Ptr result = new Poco::JSON::Object();
                result->set("kind", "full");
                result->set("items", diagnostics(documentUri(params)));
                writeFrame(response(id, Poco::Dynamic::Var(result)));
            } else if (hasId) {
                writeFrame(errorResponse(id, -32601, "unsupported LSP method: " + method));
            }
        }
        return shutdown_ ? 0 : 1;
    }

private:
    void update(const std::string& uri, std::string source) {
        if (uri.empty()) return;
        documents_[uri] = std::move(source);
        canonical_[uri] = canonicalUri(uri);
        try {
            runtime_.compileSource(documents_[uri], canonical_[uri]);
        } catch (const std::exception&) {
        }
        Poco::JSON::Object::Ptr params = new Poco::JSON::Object();
        params->set("uri", uri);
        params->set("diagnostics", diagnostics(uri));
        Poco::JSON::Object::Ptr notification = new Poco::JSON::Object();
        notification->set("jsonrpc", "2.0");
        notification->set("method", "textDocument/publishDiagnostics");
        notification->set("params", params);
        writeFrame(notification);
    }

    Poco::JSON::Array::Ptr diagnostics(const std::string& uri) const {
        Poco::JSON::Array::Ptr result = new Poco::JSON::Array();
        const auto canonical = canonical_.find(uri);
        const std::string& identity = canonical == canonical_.end() ? uri : canonical->second;
        auto diagnostics = runtime_.scriptCompiler().diagnostics(identity);
        const auto document = documents_.find(uri);
        const bool isProjectConfig = identity == "game:/config.nut" ||
                                     (uri.size() >= 11 && uri.compare(uri.size() - 11, 11, "/config.nut") == 0);
        if (document != documents_.end() && isProjectConfig) {
            auto configDiagnostics = script::ScriptCompiler::validateProjectConfig(
                document->second, identity, knownModules_, activeModules_);
            diagnostics.insert(diagnostics.end(), configDiagnostics.begin(), configDiagnostics.end());
        }
        for (const script::ScriptDiagnostic& diagnostic : diagnostics) {
            Poco::JSON::Object::Ptr start = new Poco::JSON::Object();
            start->set("line", static_cast<int>(diagnostic.position.line - 1));
            start->set("character", static_cast<int>(diagnostic.position.column - 1));
            Poco::JSON::Object::Ptr end = new Poco::JSON::Object();
            end->set("line", static_cast<int>(diagnostic.position.line - 1));
            end->set("character", static_cast<int>(diagnostic.position.column));
            Poco::JSON::Object::Ptr range = new Poco::JSON::Object();
            range->set("start", start);
            range->set("end", end);
            Poco::JSON::Object::Ptr item = new Poco::JSON::Object();
            item->set("range", range);
            item->set("severity", diagnostic.severity == script::ScriptDiagnosticSeverity::Error ? 1 : 2);
            item->set("code", diagnostic.code);
            item->set("source", "evescript");
            item->set("message", diagnostic.message);
            result->add(item);
        }
        return result;
    }

    Poco::JSON::Array::Ptr completion(const Poco::JSON::Object::Ptr& params) const {
        Poco::JSON::Array::Ptr result = new Poco::JSON::Array();
        const std::string uri = documentUri(params);
        const auto        found = documents_.find(uri);
        std::string       prefix;
        if (found != documents_.end() && params) {
            auto position = params->getObject("position");
            if (position)
                prefix = wordAt(found->second, static_cast<size_t>(position->optValue<int>("line", 0)),
                                static_cast<size_t>(position->optValue<int>("character", 0)));
        }
        const auto canonical = canonical_.find(uri);
        const std::string& identity = canonical == canonical_.end() ? uri : canonical->second;
        for (const script::ScriptCompletion& completion : runtime_.scriptCompiler().completions(identity, prefix)) {
            Poco::JSON::Object::Ptr item = new Poco::JSON::Object();
            item->set("label", completion.label);
            item->set("detail", completion.detail);
            item->set("insertText", completion.insertText);
            item->set("kind", completion.kind == "function" ? 3 : 6);
            result->add(item);
        }
        return result;
    }

    Poco::Dynamic::Var hover(const Poco::JSON::Object::Ptr& params) const {
        const std::string uri = documentUri(params);
        const auto        found = documents_.find(uri);
        if (found == documents_.end() || !params) return {};
        auto position = params->getObject("position");
        if (!position) return {};
        const std::string symbol = wordAt(found->second, static_cast<size_t>(position->optValue<int>("line", 0)),
                                          static_cast<size_t>(position->optValue<int>("character", 0)));
        const auto canonical = canonical_.find(uri);
        const std::string& identity = canonical == canonical_.end() ? uri : canonical->second;
        const auto value = runtime_.scriptCompiler().hover(identity, symbol);
        if (!value) return {};
        Poco::JSON::Object::Ptr contents = new Poco::JSON::Object();
        contents->set("kind", "markdown");
        contents->set("value", value->markdown);
        Poco::JSON::Object::Ptr result = new Poco::JSON::Object();
        result->set("contents", contents);
        return Poco::Dynamic::Var(result);
    }

    std::string canonicalUri(const std::string& uri) const {
        constexpr std::string_view filePrefix = "file:///";
        if (uri.rfind(filePrefix, 0) != 0) return uri;
        std::string path = uri.substr(filePrefix.size());
#if defined(_WIN32)
        if (path.size() >= 2 && path[1] == ':') {
        } else {
            path.insert(path.begin(), '/');
        }
#else
        path.insert(path.begin(), '/');
#endif
        std::error_code error;
        const auto relative = std::filesystem::relative(std::filesystem::path(path), std::filesystem::path(root_), error);
        if (error || relative.empty() || relative.generic_string().rfind("..", 0) == 0) return uri;
        return "game:/" + relative.generic_string();
    }

    std::string                                  root_;
    Runtime                                      runtime_;
    std::unordered_map<std::string, std::string> documents_;
    std::unordered_map<std::string, std::string> canonical_;
    std::vector<std::string>                     knownModules_;
    std::vector<std::string>                     activeModules_;
    bool                                         shutdown_ = false;
};

}  // namespace
#endif

struct LanguageServerArgs : Handler {
    std::string root;

    void setup(CLI::App& app, std::shared_ptr<CLI::Formatter> formatter) override {
        auto command = app.add_subcommand("language-server", "Run the EveScript LSP server over stdio");
        command->formatter(formatter);
        command->add_option("-r,--root", root, "Project root used to resolve game:/ modules");
    }

    int parse(CLI::App& app, Cmdline& cmd) override {
        auto command = app.get_subcommand("language-server");
        return command->parsed() ? cmd.LanguageServer(root) : -1;
    }
};

CMD_REG(LanguageServerArgs);

int Cmdline::LanguageServer(std::string path) {
#if defined(EVENGINE_ANDROID) || defined(EVENGINE_IOS) || defined(EVENGINE_WEBGPU)
    (void)path;
    std::cerr << "eve language-server: desktop-only" << std::endl;
    return 4;
#else
    std::error_code error;
    if (path.empty()) path = std::filesystem::current_path(error).string();
    LanguageServerSession session(std::move(path));
    return session.run();
#endif
}

}  // namespace eve::cmd
