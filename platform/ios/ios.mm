#include "ios.h"

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <mach-o/dyld.h>
#include <sys/stat.h>

#include <string>
#include <vector>

namespace eve {
namespace ios {

namespace {

bool directoryExists(const std::string &path) {
    if (path.empty())
        return false;
    struct stat st {};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string nsToStd(NSString *s) {
    if (s == nil)
        return {};
    const char *utf8 = [s UTF8String];
    return utf8 ? std::string(utf8) : std::string();
}

id audioInterruptionObserver = nil;

}  // namespace

std::string getResources(bool &fused) {
    fused = false;
    @autoreleasepool {
        NSString *resPath = [[NSBundle mainBundle] resourcePath];
        if (resPath == nil)
            return {};

        NSArray *contents = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:resPath error:nil];
        if (contents != nil) {
            for (NSString *file in contents) {
                NSString *lower = [file lowercaseString];
                if ([lower hasSuffix:@".love"] || [lower hasSuffix:@".eve"]) {
                    fused = true;
                    return nsToStd([resPath stringByAppendingPathComponent:file]);
                }
            }
        }

        NSString *gameDir = [resPath stringByAppendingPathComponent:@"game"];
        BOOL isDir = NO;
        if ([[NSFileManager defaultManager] fileExistsAtPath:gameDir isDirectory:&isDir] && isDir)
            return nsToStd(gameDir);

        return nsToStd(resPath);
    }
}

std::string getGameDirectory() {
    bool fused = false;
    std::string resources = getResources(fused);
    if (!fused && !resources.empty())
        return resources;

    @autoreleasepool {
        NSString *resPath = [[NSBundle mainBundle] resourcePath];
        if (resPath == nil)
            return {};
        NSString *gameDir = [resPath stringByAppendingPathComponent:@"game"];
        BOOL isDir = NO;
        if ([[NSFileManager defaultManager] fileExistsAtPath:gameDir isDirectory:&isDir] && isDir)
            return nsToStd(gameDir);
        return nsToStd(resPath);
    }
}

std::string getAppdataDirectory() {
    @autoreleasepool {
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES);
        if ([paths count] == 0)
            return {};
        NSString *base = paths[0];
        NSString *app = [base stringByAppendingPathComponent:@"EVEngine"];
        [[NSFileManager defaultManager] createDirectoryAtPath:app
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:nil];
        return nsToStd(app);
    }
}

std::string getHomeDirectory() {
    @autoreleasepool {
        NSString *home = NSHomeDirectory();
        return nsToStd(home);
    }
}

bool openURL(const std::string &url) {
    if (url.empty())
        return false;
    @autoreleasepool {
        NSURL *nsurl = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        if (nsurl == nil)
            return false;
        UIApplication *app = [UIApplication sharedApplication];
        if (![app canOpenURL:nsurl])
            return false;
        [app openURL:nsurl options:@{} completionHandler:nil];
        return true;
    }
}

std::string getExecutablePath() {
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0)
        return {};

    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        return {};
    return std::string(buffer.data());
}

void vibrate() {
    AudioServicesPlaySystemSound(kSystemSoundID_Vibrate);
}

bool setAudioMixWithOthers(bool mixEnabled) {
    @autoreleasepool {
        AVAudioSession *session = [AVAudioSession sharedInstance];
        NSError *error = nil;
        AVAudioSessionCategoryOptions options = AVAudioSessionCategoryOptionDefaultToSpeaker;
        if (mixEnabled)
            options |= AVAudioSessionCategoryOptionMixWithOthers;
        BOOL ok = [session setCategory:AVAudioSessionCategoryAmbient
                          withOptions:options
                                error:&error];
        if (!ok)
            return false;
        return [session setActive:YES error:&error] == YES;
    }
}

bool hasBackgroundMusic() {
    @autoreleasepool {
        return [[AVAudioSession sharedInstance] isOtherAudioPlaying] == YES;
    }
}

void initAudioSessionInterruptionHandler() {
    @autoreleasepool {
        if (audioInterruptionObserver != nil)
            return;
        AVAudioSession *session = [AVAudioSession sharedInstance];
        audioInterruptionObserver =
            [[NSNotificationCenter defaultCenter]
                addObserverForName:AVAudioSessionInterruptionNotification
                            object:session
                             queue:[NSOperationQueue mainQueue]
                        usingBlock:^(NSNotification *note) {
                          NSNumber *type = note.userInfo[AVAudioSessionInterruptionTypeKey];
                          if (type == nil)
                              return;
                          if (type.unsignedIntegerValue == AVAudioSessionInterruptionTypeEnded) {
                              NSError *error = nil;
                              [[AVAudioSession sharedInstance] setActive:YES error:&error];
                          }
                        }];
        NSError *error = nil;
        [session setCategory:AVAudioSessionCategoryAmbient error:&error];
        [session setActive:YES error:&error];
    }
}

void destroyAudioSessionInterruptionHandler() {
    @autoreleasepool {
        if (audioInterruptionObserver == nil)
            return;
        [[NSNotificationCenter defaultCenter] removeObserver:audioInterruptionObserver];
        audioInterruptionObserver = nil;
    }
}

Rect getSafeArea(SDL_Window *window) {
    Rect area{0, 0, 0, 0};
    if (!window)
        return area;

    int w = 0, h = 0;
    SDL_GetWindowSize(window, &w, &h);
    area.w = w;
    area.h = h;

    @autoreleasepool {
        SDL_SysWMinfo info;
        SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(window, &info))
            return area;

        UIWindow *uiWindow = info.info.uikit.window;
        if (uiWindow == nil)
            return area;

        UIView *view = uiWindow.rootViewController.view;
        if (view == nil)
            view = uiWindow;
        if (view == nil)
            return area;

        UIEdgeInsets insets = UIEdgeInsetsZero;
        if (@available(iOS 11.0, *))
            insets = view.safeAreaInsets;

        const CGFloat scale = view.contentScaleFactor > 0.0 ? view.contentScaleFactor : 1.0;
        // Convert points to the same coordinate space SDL reports for window size
        // (points, not pixels) so callers can use them with getWidth/getHeight.
        area.x = (int)insets.left;
        area.y = (int)insets.top;
        area.w = (int)(view.bounds.size.width - insets.left - insets.right);
        area.h = (int)(view.bounds.size.height - insets.top - insets.bottom);
        (void)scale;
        if (area.w < 0)
            area.w = 0;
        if (area.h < 0)
            area.h = 0;
    }
    return area;
}

}  // namespace ios
}  // namespace eve
