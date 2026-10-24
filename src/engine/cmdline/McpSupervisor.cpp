#include "McpSupervisor.h"

#if !defined(EVENGINE_ANDROID) && !defined(EVENGINE_IOS) && !defined(EVENGINE_WEBGPU)
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <Poco/Pipe.h>
#include <Poco/PipeStream.h>
#include <Poco/Process.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <filesystem>
#include <future>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#endif

namespace eve::cmd::detail {
namespace {
using Object = Poco::JSON::Object;
using Clock  = std::chrono::steady_clock;

std::string json(const Poco::Dynamic::Var& value) {
    std::ostringstream out;
    Poco::JSON::Stringifier::stringify(value, out);
    return out.str();
}

std::string executablePath() {
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const auto   size = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (size == 0 || size >= buffer.size()) throw std::runtime_error("cannot locate eve executable");
    buffer.resize(size);
    return std::filesystem::path(buffer).string();
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) throw std::runtime_error("cannot locate eve executable");
    return std::filesystem::canonical(buffer.c_str()).string();
#else
    return std::filesystem::read_symlink("/proc/self/exe").string();
#endif
}

#ifndef _WIN32
volatile std::sig_atomic_t workerPid = 0;
// Only async-signal-safe OS calls: do not leave an owned VM running when the
// MCP client terminates its supervisor while stdin is blocked.
void terminateSupervisor(int signal) {
    const auto pid = static_cast<pid_t>(workerPid);
    if (pid > 0) {
        ::kill(pid, SIGKILL);
        while (::waitpid(pid, nullptr, 0) < 0 && errno == EINTR) {
        }
    }
    _exit(128 + signal);
}
#endif

class DeadlineExceeded : public std::runtime_error {
public:
    DeadlineExceeded() : std::runtime_error("execution deadline exceeded") {}
};

class Supervisor {
public:
    Supervisor(std::string root, int timeoutMs)
        : executable_(executablePath()), root_(std::move(root)), timeout_(timeoutMs) {
#ifdef _WIN32
        job_ = CreateJobObjectW(nullptr, nullptr);
        if (!job_) throw std::runtime_error("cannot create MCP worker job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
            CloseHandle(job_);
            throw std::runtime_error("cannot configure MCP worker job");
        }
#endif
    }
    ~Supervisor() {
        stop();
#ifdef _WIN32
        CloseHandle(job_);
#endif
    }

    void serve() {
        std::string line;
        while (std::getline(std::cin, line)) {
            if (line.empty()) continue;
            Object::Ptr request;
            try {
                Poco::JSON::Parser parser;
                request = parser.parse(line).extract<Object::Ptr>();
                if (!request || request->getValue<std::string>("jsonrpc") != "2.0" || !request->has("method"))
                    throw std::runtime_error("invalid request");
            } catch (const std::exception&) {
                Object::Ptr error = new Object;
                error->set("code", -32600);
                error->set("message", "Invalid Request");
                Object::Ptr response = new Object;
                response->set("jsonrpc", "2.0");
                response->set("id", Poco::Dynamic::Var());
                response->set("error", error);
                std::cout << json(response) << std::endl;
                continue;
            }
            auto              params = request->getObject("params");
            const std::string tool   = params ? params->optValue<std::string>("name", "") : "";
            if (tool == "eve_error_slice" && !lastDiagnostic_.empty()) {
                if (request->has("id")) reply(request, lastDiagnostic_, false);
                continue;
            }
            try {
                if (!process_) start();
                const std::string response = exchange(line, request);
                if (!request->has("id")) continue;
                if (request->getValue<std::string>("method") == "initialize") {
                    Poco::JSON::Parser parser;
                    if (parser.parse(response).extract<Object::Ptr>()->has("result")) initialization_ = line;
                }
                if (tool == "eve_run_script") {
                    Poco::JSON::Parser parser;
                    auto               result = parser.parse(response).extract<Object::Ptr>()->getObject("result");
                    if (result && result->optValue<bool>("isError", false)) lastDiagnostic_.clear();
                }
                std::cout << response << std::endl;
            } catch (const std::exception& error) {
                // exchange has already joined its I/O task after killing the
                // worker. Never label a mere client wait timeout as stopped.
                stop();
                Object::Ptr diagnostic = new Object;
                diagnostic->set("schema", "eve.mcp.watchdog");
                diagnostic->set("version", 1);
                diagnostic->set("type",
                                dynamic_cast<const DeadlineExceeded*>(&error) ? "execution_timeout" : "host_failure");
                diagnostic->set("origin", tool.empty() ? request->getValue<std::string>("method") : tool);
                if (tool == "eve_run_script") diagnostic->set("source", "memory://mcp/snippet");
                diagnostic->set("budget_ms", timeout_.count());
                diagnostic->set("message", error.what());
                diagnostic->set("vm_state", "terminated");
                diagnostic->set("recovery", "next request starts a fresh host; in-memory state is lost");
                lastDiagnostic_ = json(diagnostic);
                if (request->has("id")) reply(request, lastDiagnostic_, true);
            }
        }
    }

private:
    void reply(const Object::Ptr& request, const std::string& message, bool isError) {
        Object::Ptr item = new Object;
        item->set("type", "text");
        item->set("text", message);
        Poco::JSON::Array::Ptr content = new Poco::JSON::Array;
        content->add(item);
        Object::Ptr result = new Object;
        result->set("content", content);
        result->set("isError", isError);
        Object::Ptr response = new Object;
        response->set("jsonrpc", "2.0");
        response->set("id", request->get("id"));
        if (request->getValue<std::string>("method") == "tools/call") {
            response->set("result", result);
        } else {
            Object::Ptr error = new Object;
            error->set("code", -32000);
            error->set("message", message);
            response->set("error", error);
        }
        std::cout << json(response) << std::endl;
    }

    void start() {
        input_   = std::make_unique<Poco::Pipe>();
        output_  = std::make_unique<Poco::Pipe>();
        process_ = std::make_unique<Poco::ProcessHandle>(Poco::Process::launch(
            executable_, {"mcp", "--worker", "--root", root_}, input_.get(), output_.get(), nullptr));
#ifdef _WIN32
        HANDLE     child    = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, process_->id());
        const bool assigned = child && AssignProcessToJobObject(job_, child);
        if (child) CloseHandle(child);
        if (!assigned) {
            stop();
            throw std::runtime_error("cannot assign MCP worker to lifetime job");
        }
#else
        workerPid = static_cast<std::sig_atomic_t>(process_->id());
#endif
        reader_ = std::make_unique<Poco::PipeInputStream>(*output_);
        if (!initialization_.empty()) {
            Poco::JSON::Parser parser;
            auto               request     = parser.parse(initialization_).extract<Object::Ptr>();
            const auto         response    = exchange(initialization_, request);
            auto               initialized = parser.parse(response).extract<Object::Ptr>();
            if (initialized->has("error")) throw std::runtime_error("worker initialization failed");
            Object::Ptr notification = new Object;
            notification->set("jsonrpc", "2.0");
            notification->set("method", "notifications/initialized");
            exchange(json(notification), notification);
        }
    }

    void stop() {
        if (!process_) return;
        if (process_->tryWait() == -1) {
            Poco::Process::kill(*process_);
            const int exitCode = process_->wait();
            (void)exitCode;  // Process is reaped; any exit status discards this VM.
        }
#ifndef _WIN32
        workerPid = 0;
#endif
        process_.reset();
        // exchange may still own the pipe/reader until its async task joins.
    }

    std::string exchange(const std::string& line, const Object::Ptr& request) {
        const auto deadline = Clock::now() + timeout_;
        auto       task     = std::async(std::launch::async, [this, line, request]() -> std::string {
            const std::string wire   = line + '\n';
            std::size_t       offset = 0;
            while (offset < wire.size()) {
                const int count = input_->writeBytes(
                    wire.data() + offset, static_cast<int>(std::min<std::size_t>(wire.size() - offset, INT_MAX)));
                if (count <= 0) throw std::runtime_error("worker stdin closed");
                offset += static_cast<std::size_t>(count);
            }
            if (!request->has("id")) return {};
            std::string response;
            while (std::getline(*reader_, response)) {
                try {
                    Poco::JSON::Parser parser;
                    auto               object = parser.parse(response).extract<Object::Ptr>();
                    if (object->has("id") && json(object->get("id")) == json(request->get("id"))) return response;
                    if (!object->has("id")) std::cout << response << std::endl;
                } catch (const std::exception&) {
                    std::cerr << "eve mcp: ignored malformed worker output\n";
                }
            }
            throw std::runtime_error("worker disconnected");
        });
        if (task.wait_until(deadline) != std::future_status::ready) {
            stop();
            task.wait();
            throw DeadlineExceeded();
        }
        return task.get();
    }

    std::string                            executable_, root_, initialization_, lastDiagnostic_;
    std::chrono::milliseconds              timeout_;
    std::unique_ptr<Poco::ProcessHandle>   process_;
    std::unique_ptr<Poco::Pipe>            input_, output_;
    std::unique_ptr<Poco::PipeInputStream> reader_;
#ifdef _WIN32
    HANDLE job_ = nullptr;
#endif
};
}  // namespace

int runMcpSupervisor(const std::string& root, int timeoutMs) {
#ifndef _WIN32
    const auto oldTerm = std::signal(SIGTERM, terminateSupervisor);
    const auto oldInt  = std::signal(SIGINT, terminateSupervisor);
    const auto oldPipe = std::signal(SIGPIPE, SIG_IGN);
#endif
    int status = 0;
    try {
        Supervisor supervisor(std::filesystem::absolute(root).string(), timeoutMs);
        supervisor.serve();
    } catch (const std::exception& error) {
        std::cerr << "eve mcp supervisor: " << error.what() << std::endl;
        status = 3;
    }
#ifndef _WIN32
    std::signal(SIGTERM, oldTerm);
    std::signal(SIGINT, oldInt);
    std::signal(SIGPIPE, oldPipe);
#endif
    return status;
}
}  // namespace eve::cmd::detail
#endif
