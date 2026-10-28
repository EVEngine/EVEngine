#include "common/BuildInfo.h"
#include "common/Version.h"
#include "common/config.h"

#include <string>

namespace eve {

namespace {
constexpr const char* kGitCommit  = EVENGINE_GIT_COMMIT;
constexpr int         kGitDirty   = EVENGINE_GIT_DIRTY;
constexpr const char* kBuildTime  = EVENGINE_BUILD_TIME;
constexpr const char* kBuildType  = EVENGINE_BUILD_TYPE;
constexpr const char* kThirdParty = EVENGINE_TP_DESC;
}  // namespace

const char* gitCommit() { return kGitCommit; }
const char* thirdPartyVersion() { return kThirdParty; }

std::string buildInfo() {
    std::string info = EVENGINE_VERSION;
    info += " git=";
    info += kGitCommit;
    if (kGitDirty) info += "+dirty";
    info += " built=";
    info += kBuildTime;
    info += " [";
    info += kBuildType;
    info += "] tp=";
    info += kThirdParty;
    return info;
}

}  // namespace eve
