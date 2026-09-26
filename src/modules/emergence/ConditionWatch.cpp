#include "emergence/ConditionWatch.h"

#include "emergence/FactStore.h"

#include "common/Diagnostic.h"

#include <algorithm>
#include <set>

namespace eve::emergence {
namespace {

void insertUnique(std::set<std::string>& out, std::string key) {
    if (!key.empty()) out.insert(std::move(key));
}

eve::Result<void> walk(const decision::Condition& condition, std::set<std::string>& out) {
    using K = decision::ConditionKind;
    switch (condition.kind()) {
        case K::All:
        case K::Any:
            for (const auto& child : condition.children()) {
                auto nested = walk(child, out);
                if (!nested) return nested;
            }
            return eve::Result<void>::success();
        case K::Not:
            if (condition.children().empty())
                return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                                         "not condition requires a child", "children",
                                                                         {}, "emergence.watch"));
            return walk(condition.children().front(), out);
        case K::Compare:
            insertUnique(out, makeFactKey(FactDomain::Value, condition.key()));
            return eve::Result<void>::success();
        case K::HasTag:
            insertUnique(out, makeFactKey(FactDomain::Tag, condition.key()));
            return eve::Result<void>::success();
        case K::HasAttribute:
            insertUnique(out, makeFactKey(FactDomain::Attribute, condition.key()));
            return eve::Result<void>::success();
        case K::HasResource:
            insertUnique(out, makeFactKey(FactDomain::Resource, condition.key()));
            return eve::Result<void>::success();
        case K::StateEquals:
            insertUnique(out, makeFactKey(FactDomain::State, condition.key()));
            return eve::Result<void>::success();
        case K::AuthorityCheck:
            insertUnique(out, makeFactKey(FactDomain::Authority, condition.key()));
            return eve::Result<void>::success();
        case K::PolicyCall: {
            insertUnique(out, makeFactKey(FactDomain::Policy, condition.key()));
            const auto& declaration = condition.scriptDeclaration();
            if (declaration) {
                for (const auto& dependency : declaration->dependencies) insertUnique(out, dependency);
            }
            if (out.empty() || (declaration && declaration->dependencies.empty() && condition.key().empty())) {
                return eve::Result<void>::failure(eve::Diagnostic::error(
                    eve::DiagnosticCode::InvalidArgument,
                    "policy_call conditions require a policy name or declared dependencies for indexing", "policy", {},
                    "emergence.watch"));
            }
            return eve::Result<void>::success();
        }
    }
    return eve::Result<void>::failure(eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                                             "unknown condition kind while collecting watch keys",
                                                             "kind", {}, "emergence.watch"));
}

}  // namespace

eve::Result<std::vector<std::string>> collectWatchKeys(const decision::Condition& condition) {
    if (!condition.isValid())
        return eve::Result<std::vector<std::string>>::failure(eve::Diagnostic::error(
            eve::DiagnosticCode::InvalidArgument, "condition tree is invalid", "condition", {}, "emergence.watch"));
    std::set<std::string> unique;
    auto                  walked = walk(condition, unique);
    if (!walked) return eve::Result<std::vector<std::string>>::failure(walked.status());
    if (unique.empty())
        return eve::Result<std::vector<std::string>>::failure(
            eve::Diagnostic::error(eve::DiagnosticCode::InvalidArgument,
                                   "condition produced no watch keys; empty All nodes cannot be indexed alone",
                                   "condition", {}, "emergence.watch"));
    return eve::Result<std::vector<std::string>>::success(std::vector<std::string>(unique.begin(), unique.end()));
}

}  // namespace eve::emergence
