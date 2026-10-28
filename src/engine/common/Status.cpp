#include "common/Status.h"

#include <ostream>

namespace eve {

std::ostream& operator<<(std::ostream& stream, StatusCode code) { return stream << statusCodeName(code); }


Status::Status() noexcept                    = default;
Status::~Status() noexcept                   = default;
Status::Status(const Status&)                = default;
Status& Status::operator=(const Status&)     = default;
Status::Status(Status&&) noexcept            = default;
Status& Status::operator=(Status&&) noexcept = default;
Status::Status(StatusCode code, std::vector<Diagnostic> diagnostics) : code_(code) {
    if (!diagnostics.empty()) diagnostics_ = std::make_shared<const std::vector<Diagnostic>>(std::move(diagnostics));
}
Status Status::success(StatusCode code) {
    Status result;
    result.code_ = code;
    return result;
}
const std::vector<Diagnostic>& Status::emptyDiagnostics() noexcept {
    static const std::vector<Diagnostic> empty;
    return empty;
}
Status Status::failure(StatusCode code, Diagnostic diagnostic) {
    std::vector<Diagnostic> diagnostics;
    diagnostics.emplace_back(std::move(diagnostic));
    return Status(code, std::move(diagnostics));
}
Status Status::failure(Diagnostic diagnostic) {
    const StatusCode code = statusCodeFor(diagnostic.code());
    return failure(code, std::move(diagnostic));
}

std::string Status::describe() const {
    /** @brief Result. */
    std::string result(statusCodeName(code_));
    if (const Diagnostic* diagnostic = primaryDiagnostic()) {
        result += ": ";
        result += diagnostic->message();
        if (!diagnostic->path().empty()) {
            result += " [";
            result += diagnostic->path();
            result += ']';
        }
    }
    return result;
}

}  // namespace eve
