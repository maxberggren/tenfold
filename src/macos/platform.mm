// tenfold on macOS: the trackpad through MultitouchSupport, sound through
// CoreAudio, the Open and Save panels through AppKit.

#include "platform.h"

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <memory>

#include "coreaudio.h"
#include "filepicker.h"
#include "microphone.h"
#include "trackpad.h"

namespace tf {

bool microphoneDenied() {
    const AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    return status == AVAuthorizationStatusDenied || status == AVAuthorizationStatusRestricted;
}

void askForMicrophone() {
    if ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio] == AVAuthorizationStatusNotDetermined)
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                 completionHandler:^(BOOL) {
                                 }];
}

namespace {

UTType *songType() { return [UTType typeWithFilenameExtension:@"tenfold"]; }

}

void MacFilePicker::openFile(const QString &title, std::function<void(const QString &)> done) {
    NSOpenPanel *panel = [NSOpenPanel openPanel];
    panel.message = title.toNSString();
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    if (UTType *type = songType())
        panel.allowedContentTypes = @[ type ];
    auto answer = std::make_shared<std::function<void(const QString &)>>(std::move(done));
    [panel beginWithCompletionHandler:^(NSModalResponse response) {
        (*answer)(response == NSModalResponseOK && panel.URL ? QString::fromNSString(panel.URL.path) : QString());
    }];
}

void MacFilePicker::saveFile(const QString &title, const QString &name, const QString &folder,
                             std::function<void(const QString &)> done) {
    NSSavePanel *panel = [NSSavePanel savePanel];
    panel.message = title.toNSString();
    panel.nameFieldStringValue = name.toNSString();
    if (!folder.isEmpty())
        panel.directoryURL = [NSURL fileURLWithPath:folder.toNSString() isDirectory:YES];
    if (UTType *type = songType())
        panel.allowedContentTypes = @[ type ];
    auto answer = std::make_shared<std::function<void(const QString &)>>(std::move(done));
    [panel beginWithCompletionHandler:^(NSModalResponse response) {
        (*answer)(response == NSModalResponseOK && panel.URL ? QString::fromNSString(panel.URL.path) : QString());
    }];
}

namespace platform {

std::unique_ptr<Pad> openPad(FrameSink *sink) { return std::make_unique<Trackpad>(sink); }

std::unique_ptr<Audio> openAudio(Studio *studio) { return std::make_unique<CoreAudioOut>(studio); }

std::unique_ptr<FilePicker> filePicker(std::function<void(const QString &)>) {
    return std::make_unique<MacFilePicker>();
}

int runSystemCommand(int, char **) { return -1; }

int measureLatency(bool json) { return reportCoreAudioLatency(json); }

}

}
