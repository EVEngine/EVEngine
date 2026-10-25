#include "graphics/ViewPreparation.h"
#include <algorithm>
#include <exception>
#include <vector>
#include "common/Assert.h"
namespace eve::graphics {
namespace {
struct Entry {
    uint64_t        token;
    ViewPreparation callback;
};
std::vector<Entry> entries;
uint64_t           nextToken   = 1;
bool               dispatching = false;
Diagnostic         rejected(const char* message) {
    return Diagnostic::error(DiagnosticCode::InvalidArgument, message, "graphics.view-preparation");
}
}  // namespace
Result<uint64_t> addViewPreparation(ViewPreparation callback) {
    if (!callback || dispatching || nextToken == 0)
        return Result<uint64_t>::failure(rejected("Invalid or reentrant view preparation registration"));
    try {
        const auto token = nextToken;
        entries.push_back({token, std::move(callback)});
        ++nextToken;
        return Result<uint64_t>::success(token);
    } catch (const std::exception& error) {
        return Result<uint64_t>::failure(rejected(error.what()));
    }
}
void removeViewPreparation(uint64_t token) noexcept {
    EV_ASSERT(!dispatching, "View preparers must detach outside dispatch");
    if (dispatching) return;
    std::erase_if(entries, [token](const Entry& entry) { return entry.token == token; });
}
Result<void> detail::prepareViewResources(Graphics& graphics, const glm::mat4& vp, const glm::vec3& eye) {
    if (dispatching) return Result<void>::failure(rejected("View preparation cannot recursively dispatch"));
    struct Guard {
        Guard() { dispatching = true; }
        ~Guard() { dispatching = false; }
    } guard;
    try {
        for (const auto& entry : entries) {
            auto prepared = entry.callback(graphics, vp, eye);
            if (!prepared) return Result<void>::failure(prepared.status());
        }
        return Result<void>::success();
    } catch (const std::exception& error) {
        return Result<void>::failure(rejected(error.what()));
    }
}
}  // namespace eve::graphics
