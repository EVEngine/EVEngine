#include "common/Diagnostic.h"

namespace eve {
Diagnostic::Diagnostic() noexcept                              = default;
Diagnostic::~Diagnostic()                                      = default;
Diagnostic::Diagnostic(const Diagnostic& other)                = default;
Diagnostic::Diagnostic(Diagnostic&& other) noexcept            = default;
Diagnostic& Diagnostic::operator=(const Diagnostic& other)     = default;
Diagnostic& Diagnostic::operator=(Diagnostic&& other) noexcept = default;

Diagnostic::Diagnostic(DiagnosticCode code, Severity severity, std::string message, std::string path)
    : Diagnostic(code, severity, std::move(message), std::move(path), {}, {}) {}

Diagnostic Diagnostic::error(DiagnosticCode code, std::string message, std::string path) {
    return Diagnostic(code, Severity::Error, std::move(message), std::move(path));
}
Diagnostic Diagnostic::warning(DiagnosticCode code, std::string message, std::string path) {
    return Diagnostic(code, Severity::Warning, std::move(message), std::move(path));
}

Diagnostic::Diagnostic(DiagnosticCode code, Severity severity, std::string message, std::string path,
                       DiagnosticDetails details, std::string source)
    : code_(code),
      severity_(severity),
      message_(std::move(message)),
      path_(std::move(path)),
      details_(std::move(details)),
      source_(std::move(source)) {}

Diagnostic Diagnostic::error(DiagnosticCode code, std::string message, std::string path, DiagnosticDetails details,
                             std::string source) {
    return Diagnostic(code, Severity::Error, std::move(message), std::move(path), std::move(details),
                      std::move(source));
}
Diagnostic Diagnostic::warning(DiagnosticCode code, std::string message, std::string path, DiagnosticDetails details,
                               std::string source) {
    return Diagnostic(code, Severity::Warning, std::move(message), std::move(path), std::move(details),
                      std::move(source));
}
void Diagnostic::addDetail(std::string key, std::string value) {
    details_.emplace_back(std::move(key), std::move(value));
}
}  // namespace eve


#if defined(_WIN32) && defined(EVENGINE_MODULE_DLL)
template class __declspec(dllexport) std::vector<eve::Diagnostic>;
#endif
