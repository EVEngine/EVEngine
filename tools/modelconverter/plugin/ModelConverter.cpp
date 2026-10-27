#include "ModelConverter.h"

#include "common/Exception.h"

#include <simplesquirrel/simplesquirrel.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#  include <direct.h>
#  include <windows.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#endif

namespace eve::modelconverter {

namespace {

bool fileExists(const std::string &path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

bool makeDir(const std::string &path) {
    if (path.empty()) return true;
    if (fileExists(path)) return true;
#if defined(_WIN32)
    return _mkdir(path.c_str()) == 0;
#else
    return mkdir(path.c_str(), 0755) == 0;
#endif
}

std::string joinPath(const std::string &a, const std::string &b) {
    if (a.empty()) return b;
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + "/" + b;
}

std::string quote(const std::string &s) {
    return "\"" + s + "\"";
}

// Escape a string for inclusion inside a JSON string literal.
std::string jsonEscape(const std::string &s) {
    std::ostringstream out;
    for (char c : s) {
        switch (c) {
            case '"':  out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\n': out << "\\n";  break;
            case '\r': out << "\\r";  break;
            case '\t': out << "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out << buf;
                } else {
                    out << c;
                }
        }
    }
    return out.str();
}

}  // namespace

// ---- Params ----

void ModelConverterParams::setInt(const std::string &key, int value) {
    values_[key] = std::to_string(value);
}

void ModelConverterParams::setFloat(const std::string &key, float value) {
    std::ostringstream ss;
    ss << value;
    values_[key] = ss.str();
}

void ModelConverterParams::setString(const std::string &key, const std::string &value) {
    values_[key] = value;
}

bool ModelConverterParams::has(const std::string &key) const {
    return values_.count(key) > 0;
}

int ModelConverterParams::getInt(const std::string &key, int defaultValue) const {
    auto it = values_.find(key);
    if (it == values_.end()) return defaultValue;
    return std::atoi(it->second.c_str());
}

float ModelConverterParams::getFloat(const std::string &key, float defaultValue) const {
    auto it = values_.find(key);
    if (it == values_.end()) return defaultValue;
    return static_cast<float>(std::atof(it->second.c_str()));
}

std::string ModelConverterParams::getString(const std::string &key,
                                            const std::string &defaultValue) const {
    auto it = values_.find(key);
    if (it == values_.end()) return defaultValue;
    return it->second;
}

std::string ModelConverterParams::toJson() const {
    std::ostringstream out;
    out << "{";
    bool first = true;
    for (const auto &kv : values_) {
        if (!first) out << ",";
        first = false;
        out << "\"" << jsonEscape(kv.first) << "\":\"" << jsonEscape(kv.second) << "\"";
    }
    out << "}";
    return out.str();
}

// ---- Module ----

Module_IMPL(ModelConverter, new ModelConverter());

ModelConverter::ModelConverter() {
    const char *tmp = std::getenv("TEMP");
    if (!tmp || !*tmp) tmp = std::getenv("TMP");
    if (!tmp || !*tmp) tmp = ".";
    tempDir_ = tmp;
}

ModelConverterParams *ModelConverter::newParams() { return new ModelConverterParams(); }

bool ModelConverter::configure(const std::string &converterDir, const std::string &pythonExe,
                               const std::string &pythonRuntimeDir, const std::string &tempDir) {
    converterDir_ = converterDir;
    pythonExe_ = pythonExe;
    pythonRuntimeDir_ = pythonRuntimeDir;
    if (!tempDir.empty()) tempDir_ = tempDir;
    lastError_.clear();
    return true;
}

int ModelConverter::getConverterCount() const {
    converterIdsCache_.clear();
    if (converterDir_.empty()) return 0;
#if defined(_WIN32)
    const std::string pattern = converterDir_ + "/*";
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            const std::string name = fd.cFileName;
            if (name == "." || name == "..") continue;
            if (fileExists(joinPath(joinPath(converterDir_, name), "manifest.json")))
                converterIdsCache_.push_back(name);
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    // Non-Windows: directory scan via <dirent.h>.
    DIR *dir = opendir(converterDir_.c_str());
    if (!dir) return 0;
    struct dirent *ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        if (fileExists(joinPath(joinPath(converterDir_, ent->d_name), "manifest.json")))
            converterIdsCache_.push_back(ent->d_name);
    }
    closedir(dir);
#endif
    std::sort(converterIdsCache_.begin(), converterIdsCache_.end());
    return int(converterIdsCache_.size());
}

std::string ModelConverter::getConverterId(int index) const {
    if (converterIdsCache_.empty()) getConverterCount();
    if (index < 0 || index >= int(converterIdsCache_.size())) return {};
    return converterIdsCache_[size_t(index)];
}

bool ModelConverter::hasConverter(const std::string &id) const {
    if (converterIdsCache_.empty()) getConverterCount();
    for (const auto &cid : converterIdsCache_)
        if (cid == id) return true;
    return false;
}

bool ModelConverter::spawn(const std::string &command) {
#if defined(_WIN32)
    std::string full = "cmd /c " + command;
    return std::system(full.c_str()) == 0;
#else
    return std::system(command.c_str()) == 0;
#endif
}

bool ModelConverter::runPython(const std::vector<std::string> &args, std::string &capturedError) {
    if (pythonExe_.empty()) {
        capturedError = "modelconverter: python executable not configured (call configure)";
        return false;
    }
    makeDir(tempDir_);

    std::string prefix;
    if (!pythonRuntimeDir_.empty()) {
        // Make `-m eve_blender_converter` importable for the child process.
#if defined(_WIN32)
        prefix = "set PYTHONPATH=" + quote(pythonRuntimeDir_) + " && ";
#else
        prefix = "PYTHONPATH=" + quote(pythonRuntimeDir_) + " ";
#endif
    }

    std::ostringstream cmd;
    cmd << prefix << quote(pythonExe_) << " -m eve_blender_converter";
    for (const auto &arg : args) cmd << " " << quote(arg);

    const std::string logPath = joinPath(tempDir_, "modelconverter_last.log");
    cmd << " 2>&1 > " << quote(logPath);

    const int rc = spawn(cmd.str());

    std::string log;
    {
        std::ifstream in(logPath, std::ios::binary);
        if (in) {
            std::ostringstream ss;
            ss << in.rdbuf();
            log = ss.str();
        }
    }
    // Trim trailing whitespace.
    while (!log.empty() && (log.back() == '\n' || log.back() == '\r')) log.pop_back();

    if (rc != 0) {
        capturedError = log.empty() ? "python exited with code " + std::to_string(rc) : log;
        return false;
    }
    capturedError = log;
    return true;
}

bool ModelConverter::check() {
    std::string captured;
    std::vector<std::string> args = {"check"};
    if (!runPython(args, captured)) {
        lastError_ = captured.empty() ? "check failed" : captured;
        return false;
    }
    if (captured.find("bpy OK") == std::string::npos) {
        lastError_ = "bpy is not available: pip install bpy\n" + captured;
        return false;
    }
    lastError_.clear();
    return true;
}

bool ModelConverter::convert(const std::string &converterId, const std::string &inputModel,
                             const std::string &outputModel, const std::string &format,
                             const ModelConverterParams *params) {
    lastError_.clear();
    lastOutput_.clear();

    if (converterDir_.empty() || pythonExe_.empty()) {
        lastError_ = "modelconverter: call configure(converterDir, pythonExe, ...) first";
        return false;
    }
    if (!hasConverter(converterId)) {
        lastError_ = "modelconverter: unknown converter '" + converterId + "'";
        return false;
    }
    if (inputModel.empty() || outputModel.empty()) {
        lastError_ = "modelconverter: input and output paths are required";
        return false;
    }

    makeDir(tempDir_);
    const std::string jobPath = joinPath(tempDir_, "modelconverter_job.txt");
    const std::string resultPath = joinPath(tempDir_, "modelconverter_result.txt");

    {
        std::ofstream job(jobPath, std::ios::trunc);
        if (!job) {
            lastError_ = "modelconverter: cannot write job file " + jobPath;
            return false;
        }
        job << "converter=" << converterId << "\n";
        job << "input=" << inputModel << "\n";
        job << "output=" << outputModel << "\n";
        job << "format=" << (format.empty() ? "glb" : format) << "\n";
        job << "converter_dir=" << converterDir_ << "\n";
        job << "temp_dir=" << tempDir_ << "\n";
        job << "params=" << (params ? params->toJson() : "{}") << "\n";
    }

    std::string captured;
    std::vector<std::string> args = {"convert", jobPath};
    runPython(args, captured);

    // The result file is authoritative; python may exit non-zero yet still
    // write a useful error into it (e.g. missing bpy).
    std::ifstream result(resultPath);
    if (result) {
        std::string line;
        bool ok = false;
        std::string errorFromResult;
        while (std::getline(result, line)) {
            const auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = line.substr(0, eq);
            const std::string value = line.substr(eq + 1);
            if (key == "ok") ok = (value == "1");
            else if (key == "error" && !value.empty()) errorFromResult = value;
            else if (key == "output") lastOutput_ = value;
        }
        if (ok) return true;
        lastError_ = errorFromResult.empty() ? "converter reported failure (see log)" : errorFromResult;
        lastOutput_.clear();
        return false;
    }

    lastError_ = "modelconverter: no result file from python runtime:\n" + captured;
    return false;
}

std::string ModelConverter::lastError() const { return lastError_; }
std::string ModelConverter::lastOutput() const { return lastOutput_; }

void ModelConverter::expose(ssq::Table &table) {
    auto cls = table.addClass(name, ModelConverter::create, false);
    expose(cls);

    auto params = table.addClass<ModelConverterParams>(
        "ModelConverterParams",
        std::function<ModelConverterParams *()>([]() -> ModelConverterParams * { return nullptr; }),
        true);
    params.addFunc("setInt", &ModelConverterParams::setInt);
    params.addFunc("setFloat", &ModelConverterParams::setFloat);
    params.addFunc("setString", &ModelConverterParams::setString);
    params.addFunc("has", &ModelConverterParams::has);
    params.addFunc("getInt", &ModelConverterParams::getInt);
    params.addFunc("getFloat", &ModelConverterParams::getFloat);
    params.addFunc("getString", &ModelConverterParams::getString);
}

void ModelConverter::expose(ssq::Class &cls) {
    cls.addFunc("getName", &ModelConverter::getName);
    cls.addFunc("newParams", &ModelConverter::newParams);
    cls.addFunc("configure", &ModelConverter::configure);
    cls.addFunc("check", &ModelConverter::check);
    cls.addFunc("getConverterCount", &ModelConverter::getConverterCount);
    cls.addFunc("getConverterId", &ModelConverter::getConverterId);
    cls.addFunc("hasConverter", &ModelConverter::hasConverter);
    cls.addFunc("convert", &ModelConverter::convert);
    cls.addFunc("lastError", &ModelConverter::lastError);
    cls.addFunc("lastOutput", &ModelConverter::lastOutput);
}

}  // namespace eve::modelconverter
