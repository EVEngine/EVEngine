#include "ios_test.h"

#import <Foundation/Foundation.h>
#import <os/log.h>

#include <string>

namespace eve {
namespace ios_test {

namespace {

bool copyTree(NSString *src, NSString *dst, NSError **error) {
    NSFileManager *fm = [NSFileManager defaultManager];
    BOOL isDir = NO;
    if (![fm fileExistsAtPath:src isDirectory:&isDir] || !isDir)
        return NO;
    [fm removeItemAtPath:dst error:nil];
    return [fm copyItemAtPath:src toPath:dst error:error] == YES;
}

}  // namespace

void logLine(const char *line) {
    static os_log_t log = nil;
    if (log == nil) {
        NSString *subsystem = [[NSBundle mainBundle] bundleIdentifier];
        log = os_log_create(subsystem.UTF8String, "EVEngineTest");
    }
    os_log_info(log, "%{public}s", line ? line : "");
}

std::string stagedTestRoot() {
    // Stage once per process: the runner chdirs here and every
    // pathBesideThisSource() call resolves against the same cached root.
    static const std::string root = []() -> std::string {
        @autoreleasepool {
            NSArray *paths = NSSearchPathForDirectoriesInDomains(
                NSCachesDirectory, NSUserDomainMask, YES);
            if ([paths count] == 0)
                return {};
            NSString *root = [paths[0] stringByAppendingPathComponent:@"evengine_test"];

            NSString *bundle = [[NSBundle mainBundle] resourcePath];
            if (bundle == nil)
                return {};

            // Wipe first so assets removed from the bundle do not linger across
            // reinstalls/upgrades (same contract as EVTestActivity).
            NSFileManager *fm = [NSFileManager defaultManager];
            [fm removeItemAtPath:root error:nil];

            NSError *error = nil;
            NSString *testSrc = [bundle stringByAppendingPathComponent:@"test"];
            NSString *examplesSrc = [bundle stringByAppendingPathComponent:@"examples"];
            NSString *examplesDst = [root stringByAppendingPathComponent:@"examples"];
            if (!copyTree(testSrc, root, &error) ||
                !copyTree(examplesSrc, examplesDst, &error)) {
                NSString *msg = error != nil
                    ? [NSString stringWithFormat:@"staging test assets failed: %@", error]
                    : @"staging test assets failed: bundled test/ or examples/ missing";
                logLine(msg.UTF8String);
                return {};
            }

            const char *utf8 = root.UTF8String;
            return utf8 ? std::string(utf8) : std::string();
        }
    }();
    return root;
}

std::string launchFilter() {
    @autoreleasepool {
        NSString *filter =
            [[NSUserDefaults standardUserDefaults] stringForKey:@"evengine.test.filter"];
        if ([filter length] == 0) {
            NSArray<NSString *> *args = [[NSProcessInfo processInfo] arguments];
            for (NSString *arg in args) {
                if ([arg hasPrefix:@"--testcase="]) {
                    filter = [arg substringFromIndex:11];
                    break;
                }
            }
        }
        if ([filter length] == 0)
            return {};
        const char *utf8 = filter.UTF8String;
        return utf8 ? std::string(utf8) : std::string();
    }
}

}  // namespace ios_test
}  // namespace eve
