#import <AppKit/AppKit.h>
#include "mac_launcher.h"
#include <cstdio>

static void prepareApplication() {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    static bool launched = false;
    if (!launched) { [NSApp finishLaunching]; launched = true; }
    [NSApp activateIgnoringOtherApps:YES];
}

void MeleeShowLaunchError(const std::string& error) {
    @autoreleasepool {
        prepareApplication();
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"Could not load this disc image";
        alert.informativeText = [NSString stringWithUTF8String:error.c_str()];
        [alert addButtonWithTitle:@"Choose Another Image"];
        [alert runModal];
    }
}

std::string MeleeChooseDisc(const std::string& error) {
    @autoreleasepool {
        prepareApplication();
        if (!error.empty()) MeleeShowLaunchError(error);
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = @"Melee Native";
        panel.message = @"Choose your Super Smash Bros. Melee US 1.02 disc image.\n"
                         "Game content is read from this file and is not included with the app.\n\n"
                         "Keyboard: WASD move · X attack/confirm · Z special/back · C/V jump · Return start";
        panel.prompt = @"Load Game";
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        // Compressed images need no registered macOS UTI. Validate their
        // contents in the disc reader rather than filtering by file metadata.
        if ([panel runModal] != NSModalResponseOK) return {};
        return std::string(panel.URL.fileSystemRepresentation);
    }
}

void MeleePrepareAppLogging() {
    @autoreleasepool {
        NSString* directory = [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Logs/Melee Native"];
        if ([[NSFileManager defaultManager] createDirectoryAtPath:directory withIntermediateDirectories:YES
                                                      attributes:nil error:nil]) {
            NSString* path = [directory stringByAppendingPathComponent:@"game.log"];
            std::freopen(path.fileSystemRepresentation, "w", stderr);
            std::setvbuf(stderr, nullptr, _IONBF, 0);
        }
    }
}
