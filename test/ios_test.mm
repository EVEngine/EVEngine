#include "ios_test.h"

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <os/log.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace eve {
namespace ios_test {

namespace {

bool copyTree(NSString *src, NSString *dst, NSError **error) {
    NSFileManager *fm    = [NSFileManager defaultManager];
    BOOL           isDir = NO;
    if (![fm fileExistsAtPath:src isDirectory:&isDir] || !isDir) return NO;
    [fm removeItemAtPath:dst error:nil];
    return [fm copyItemAtPath:src toPath:dst error:error] == YES;
}

// Marker written after a successful staging pass; bundles a fingerprint of the
// bundled test/ + examples/ trees so later launches in the per-file driver
// (run/ios-test-all-debug) skip the ~26 MB copy and start tests immediately.
NSString *StageMarkerPath(NSString *root) {
    return [root stringByAppendingPathComponent:@".staged"];
}

NSString *TreeFingerprint(NSString *path) {
    NSFileManager *fm     = [NSFileManager defaultManager];
    NSDictionary *attrs   = [fm attributesOfItemAtPath:path error:nil];
    NSDate       *mtime   = attrs[NSFileModificationDate];
    if (mtime == nil) return @"missing";
    return [NSString stringWithFormat:@"%lld", (long long)(mtime.timeIntervalSince1970 * 1000.0)];
}

}  // namespace

void logLine(const char *line) {
    // stderr reaches `simctl launch --console-pty` and device syslog even when
    // the unified-log store drops or delays os_log entries (simulator).
    std::fprintf(stderr, "EVEngineTest: %s\n", line ? line : "");
    std::fflush(stderr);
    static os_log_t log = nil;
    if (log == nil) {
        NSString *subsystem = [[NSBundle mainBundle] bundleIdentifier];
        log                 = os_log_create(subsystem.UTF8String, "EVEngineTest");
    }
    os_log_info(log, "%{public}s", line ? line : "");
    NSLog(@"EVEngineTest: %s", line ? line : "");
}

std::string stagedTestRoot() {
    // Stage once per process: the runner chdirs here and every
    // pathBesideThisSource() call resolves against the same cached root.
    static const std::string root = []() -> std::string {
        @autoreleasepool {
            // Keep in sync with iosTestRoot() in PathBesideSource.h: that
            // header is included inside anonymous namespaces and cannot
            // reference this ObjC helper, so it rebuilds the same path from
            // $HOME (iOS sets HOME to the app sandbox root).
            const char *home = getenv("HOME");
            if (home == nullptr) return {};
            NSString *root = [NSString stringWithFormat:@"%s/Library/Caches/evengine_test", home];

            NSString *bundle = [[NSBundle mainBundle] resourcePath];
            if (bundle == nil) return {};

            NSFileManager *fm = [NSFileManager defaultManager];
            NSString *testSrc     = [bundle stringByAppendingPathComponent:@"test"];
            NSString *examplesSrc = [bundle stringByAppendingPathComponent:@"examples"];
            NSString *examplesDst = [root stringByAppendingPathComponent:@"examples"];

            // Skip the copy when the previous staging pass matches the current
            // bundle (fast relaunch path for per-file runs).
            NSString *expected =
                [NSString stringWithFormat:@"%@|%@", TreeFingerprint(testSrc), TreeFingerprint(examplesSrc)];
            NSString *marker = StageMarkerPath(root);
            NSString *prev   = [NSString stringWithContentsOfFile:marker
                                                         encoding:NSUTF8StringEncoding
                                                            error:nil];
            if (prev != nil && [prev isEqualToString:expected]) {
                logLine("test assets already staged (marker hit)");
            } else {
                // Wipe first so assets removed from the bundle do not linger
                // across reinstalls/upgrades (same contract as EVTestActivity).
                [fm removeItemAtPath:root error:nil];
                NSError *error = nil;
                if (!copyTree(testSrc, root, &error) ||
                    !copyTree(examplesSrc, examplesDst, &error)) {
                    NSString *msg = error != nil
                        ? [NSString stringWithFormat:@"staging test assets failed: %@", error]
                        : @"staging test assets failed: bundled test/ or examples/ missing";
                    logLine(msg.UTF8String);
                    return {};
                }
                [expected writeToFile:marker
                           atomically:YES
                             encoding:NSUTF8StringEncoding
                                error:nil];
            }

            const char *utf8 = root.UTF8String;
            return utf8 ? std::string(utf8) : std::string();
        }
    }();
    return root;
}

void keepAwake() {
    @autoreleasepool {
        [UIApplication sharedApplication].idleTimerDisabled = YES;
    }
}

std::string launchFilter() {
    @autoreleasepool {
        NSString *filter = [[NSUserDefaults standardUserDefaults] stringForKey:@"evengine.test.filter"];
        if ([filter length] == 0) {
            NSArray<NSString *> *args = [[NSProcessInfo processInfo] arguments];
            for (NSString *arg in args) {
                if ([arg hasPrefix:@"--testcase="]) {
                    filter = [arg substringFromIndex:11];
                    break;
                }
            }
        }
        if ([filter length] == 0) return {};
        const char *utf8 = filter.UTF8String;
        return utf8 ? std::string(utf8) : std::string();
    }
}

std::string launchFileFilter() {
    @autoreleasepool {
        NSString *file = [[NSUserDefaults standardUserDefaults] stringForKey:@"evengine.test.file"];
        if ([file length] == 0) {
            NSArray<NSString *> *args = [[NSProcessInfo processInfo] arguments];
            for (NSString *arg in args) {
                if ([arg hasPrefix:@"--file="]) {
                    file = [arg substringFromIndex:7];
                    break;
                }
            }
        }
        if ([file length] == 0) return {};
        const char *utf8 = file.UTF8String;
        return utf8 ? std::string(utf8) : std::string();
    }
}

}  // namespace ios_test
}  // namespace eve
