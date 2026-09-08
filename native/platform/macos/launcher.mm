#import <AppKit/AppKit.h>
#include "platform_launcher.h"
#include <cstdio>

static NSString* const discBookmarkKey = @"DiscImageBookmark";

static void prepareApplication() {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    static bool launched = false;
    if (!launched) { [NSApp finishLaunching]; launched = true; }
    [NSApp activateIgnoringOtherApps:YES];
}

static NSTextField* label(NSString* text, NSRect frame, CGFloat size, bool bold = false) {
    NSTextField* view = [NSTextField wrappingLabelWithString:text];
    view.frame = frame;
    view.selectable = NO;
    view.font = bold ? [NSFont boldSystemFontOfSize:size] : [NSFont systemFontOfSize:size];
    return view;
}

@interface MeleeDropView : NSView <NSDraggingDestination>
@property(copy) void (^receiveFile)(NSURL*);
@property BOOL busy;
@end

@implementation MeleeDropView
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    return !self.busy && [sender.draggingPasteboard canReadObjectForClasses:@[[NSURL class]]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}] ? NSDragOperationCopy : NSDragOperationNone;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    if (self.busy) return NO;
    NSArray<NSURL*>* urls = [sender.draggingPasteboard readObjectsForClasses:@[[NSURL class]]
        options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    if (urls.count != 1) return NO;
    self.receiveFile(urls.firstObject);
    return YES;
}
@end

@interface MeleeSetup : NSObject <NSWindowDelegate> {
@public
    MeleeDiscValidator validator;
    std::string result;
}
@property NSWindow* window;
@property MeleeDropView* drop;
@property NSTextField* status;
@property NSTextField* filename;
@property NSButton* choose;
@property NSButton* play;
@property NSProgressIndicator* progress;
@property NSURL* selected;
@property BOOL busy;
@property BOOL automatic;
@end

@implementation MeleeSetup
- (instancetype)init {
    if (!(self = [super init])) return nil;
    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 600, 650)
        styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
        backing:NSBackingStoreBuffered defer:NO];
    self.window.title = @"Melee Native";
    self.window.releasedWhenClosed = NO;
    self.window.delegate = self;
    NSView* content = self.window.contentView;
    NSImageView* logo = [[NSImageView alloc] initWithFrame:NSMakeRect(140, 485, 320, 140)];
    logo.image = [[NSImage alloc] initWithContentsOfFile:[[NSBundle mainBundle] pathForResource:@"SSBM" ofType:@"png"]];
    logo.imageScaling = NSImageScaleProportionallyUpOrDown;
    logo.accessibilityLabel = @"Super Smash Bros. Melee";
    [content addSubview:logo];
    NSTextField* title = label(@"macOS Port", NSMakeRect(40, 445, 520, 30), 24, true);
    title.alignment = NSTextAlignmentCenter;
    [content addSubview:title];
    self.drop = [[MeleeDropView alloc] initWithFrame:NSMakeRect(40, 260, 520, 150)];
    self.drop.wantsLayer = YES;
    self.drop.layer.cornerRadius = 12;
    self.drop.layer.borderWidth = 1;
    self.drop.layer.borderColor = NSColor.separatorColor.CGColor;
    self.drop.layer.backgroundColor = NSColor.controlBackgroundColor.CGColor;
    [self.drop registerForDraggedTypes:@[NSPasteboardTypeFileURL]];
    __weak MeleeSetup* weakSelf = self;
    self.drop.receiveFile = ^(NSURL* url) { [weakSelf validateURL:url]; };
    self.filename = label(@"Drop your disc image here", NSMakeRect(20, 105, 480, 25), 16, true);
    self.filename.alignment = NSTextAlignmentCenter;
    self.filename.maximumNumberOfLines = 1;
    self.filename.lineBreakMode = NSLineBreakByTruncatingMiddle;
    [self.drop addSubview:self.filename];
    NSTextField* formats = label(@"ISO, GCM, CISO or RVZ · US 1.02", NSMakeRect(20, 77, 480, 22), 12);
    formats.alignment = NSTextAlignmentCenter;
    formats.textColor = NSColor.secondaryLabelColor;
    [self.drop addSubview:formats];
    self.choose = [NSButton buttonWithTitle:@"Choose File…" target:self action:@selector(chooseFile:)];
    self.choose.frame = NSMakeRect(182, 24, 156, 34);
    [self.drop addSubview:self.choose];
    [content addSubview:self.drop];
    self.status = label(@"Select a Melee US 1.02 ISO image for the game assets.", NSMakeRect(40, 208, 490, 44), 12);
    self.status.textColor = NSColor.secondaryLabelColor;
    [content addSubview:self.status];
    self.progress = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(537, 225, 18, 18)];
    self.progress.style = NSProgressIndicatorStyleSpinning;
    self.progress.displayedWhenStopped = NO;
    [content addSubview:self.progress];
    [content addSubview:label(@"KEYBOARD CONTROLS", NSMakeRect(40, 176, 520, 20), 11, true)];
    [content addSubview:label(@"W A S D   Move\nX   Attack / confirm\nZ   Special / back\nC / V   Jump", NSMakeRect(40, 85, 250, 84), 13)];
    [content addSubview:label(@"Q / E   Shield\nR   Grab\nI J K L   C-stick\nReturn   Start / pause", NSMakeRect(310, 85, 250, 84), 13)];
    NSTextField* note = label(@"Experimental build. Progress is not saved.\nNo disc image is included or downloaded.", NSMakeRect(40, 25, 350, 42), 11);
    note.textColor = NSColor.secondaryLabelColor;
    [content addSubview:note];
    self.play = [NSButton buttonWithTitle:@"Play" target:self action:@selector(playGame:)];
    self.play.frame = NSMakeRect(436, 29, 124, 36);
    self.play.keyEquivalent = @"\r";
    self.play.enabled = NO;
    [content addSubview:self.play];
    [self.window center];
    return self;
}
- (void)chooseFile:(id)sender {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = @"Choose your Melee US 1.02 image";
    panel.message = @"ISO, GCM, CISO and RVZ are supported.";
    panel.prompt = @"Choose Image";
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    // Content validation also handles compressed formats without registered UTIs.
    [panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
        if (response == NSModalResponseOK) [self validateURL:panel.URL];
    }];
}
- (void)validateURL:(NSURL*)url {
    if (self.busy) return;
    self.busy = YES;
    self.drop.busy = YES;
    self.choose.enabled = NO;
    self.play.enabled = NO;
    self.selected = nil;
    self.filename.stringValue = url.lastPathComponent;
    self.filename.toolTip = url.path;
    self.status.stringValue = @"Checking your disc image…";
    self.status.textColor = NSColor.secondaryLabelColor;
    [self.progress startAnimation:nil];
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        @autoreleasepool {
            std::string error;
            NSNumber* regular = nil;
            if (![url getResourceValue:&regular forKey:NSURLIsRegularFileKey error:nil] || !regular.boolValue)
                error = "This image is unavailable. Reconnect its drive or choose another file.";
            else error = self->validator(std::string(url.fileSystemRepresentation));
            NSString* message = [NSString stringWithUTF8String:error.c_str()];
            dispatch_async(dispatch_get_main_queue(), ^{
                self.busy = NO;
                self.drop.busy = NO;
                self.choose.enabled = YES;
                [self.progress stopAnimation:nil];
                if (message.length) {
                    self.automatic = NO;
                    self.status.stringValue = message;
                    self.status.textColor = NSColor.systemRedColor;
                    self.choose.title = @"Locate / Choose File…";
                } else {
                    self.selected = url;
                    self.status.stringValue = @"Melee US 1.02 is ready. Next time, the game opens directly.";
                    self.status.textColor = NSColor.systemGreenColor;
                    self.play.enabled = YES;
                    self.choose.title = @"Change File…";
                    if (self.automatic) [self playGame:nil];
                }
            });
        }
    });
}
- (void)playGame:(id)sender {
    if (!self.selected || self.busy) return;
    NSData* bookmark = [self.selected bookmarkDataWithOptions:0 includingResourceValuesForKeys:nil
        relativeToURL:nil error:nil];
    if (!bookmark) {
        self.status.stringValue = @"Could not remember this image. Choose a file on an accessible local drive.";
        self.status.textColor = NSColor.systemRedColor;
        return;
    }
    [[NSUserDefaults standardUserDefaults] setObject:bookmark forKey:discBookmarkKey];
    result = self.selected.fileSystemRepresentation;
    [NSApp stopModalWithCode:NSModalResponseOK];
}
- (BOOL)windowShouldClose:(NSWindow*)sender {
    // Let the in-flight disc operation finish before runtime cleanup begins.
    if (self.busy) return NO;
    [NSApp stopModalWithCode:NSModalResponseCancel];
    return NO;
}
@end

std::string MeleeLaunchDisc(MeleeDiscValidator validate, bool forceSetup) {
    @autoreleasepool {
        prepareApplication();
        MeleeSetup* setup = [[MeleeSetup alloc] init];
        setup->validator = std::move(validate);
        NSData* bookmark = [[NSUserDefaults standardUserDefaults] dataForKey:discBookmarkKey];
        NSURL* remembered = nil;
        if (bookmark && !forceSetup) {
            remembered = [NSURL URLByResolvingBookmarkData:bookmark
                options:NSURLBookmarkResolutionWithoutUI | NSURLBookmarkResolutionWithoutMounting
                relativeToURL:nil bookmarkDataIsStale:nil error:nil];
            if (!remembered) {
                setup.status.stringValue = @"Your previous image is unavailable. Locate it to continue.";
                setup.choose.title = @"Locate File…";
            }
        }
        [setup.window makeKeyAndOrderFront:nil];
        // Start only once the modal loop is active, including fast validation failures.
        if (remembered) {
            setup.automatic = YES;
            dispatch_async(dispatch_get_main_queue(), ^{ [setup validateURL:remembered]; });
        }
        [NSApp runModalForWindow:setup.window];
        [setup.window orderOut:nil];
        return setup->result;
    }
}

@interface MeleeMenu : NSObject {
@public
    std::function<void()> restart;
}
@end
@implementation MeleeMenu
- (void)changeDisc:(id)sender { restart(); }
- (void)controls:(id)sender {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Keyboard controls";
    alert.informativeText = @"W A S D   Move\nX   Attack / confirm\nZ   Special / back\nC / V   Jump\nQ / E   Shield\nR   Grab\nI J K L   C-stick\nReturn   Start / pause\n\nProgress is not saved in this experimental build.";
    [alert addButtonWithTitle:@"Back to Game"];
    [alert runModal];
}
- (void)logs:(id)sender {
    NSURL* url = [NSURL fileURLWithPath:[NSHomeDirectory() stringByAppendingPathComponent:@"Library/Logs/Melee Native"]];
    [[NSWorkspace sharedWorkspace] openURL:url];
}
@end

void MeleeInstallAppMenu(std::function<void()> changeDisc) {
    @autoreleasepool {
        static MeleeMenu* actions = [[MeleeMenu alloc] init];
        actions->restart = std::move(changeDisc);
        NSMenu* bar = [[NSMenu alloc] init];
        NSMenuItem* root = [[NSMenuItem alloc] init];
        [bar addItem:root];
        NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Melee Native"];
        root.submenu = menu;
        for (NSArray* entry in @[@[@"Controls…", NSStringFromSelector(@selector(controls:)), @"k"],
                                 @[@"Change Disc Image and Restart…", NSStringFromSelector(@selector(changeDisc:)), @""],
                                 @[@"Open Logs", NSStringFromSelector(@selector(logs:)), @""]]) {
            NSMenuItem* item = [menu addItemWithTitle:entry[0] action:NSSelectorFromString(entry[1]) keyEquivalent:entry[2]];
            item.target = actions;
        }
        [menu addItem:[NSMenuItem separatorItem]];
        [menu addItemWithTitle:@"Quit Melee Native" action:@selector(terminate:) keyEquivalent:@"q"];
        NSApp.mainMenu = bar;
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
