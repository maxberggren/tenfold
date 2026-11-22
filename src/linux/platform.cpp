// tenfold on Linux: the trackpad through evdev (and pkexec when it isn't
// readable as this user), sound through PipeWire, file dialogs through
// xdg-desktop-portal.

#include "platform.h"

#include <cstring>

#include "helper.h"
#include "pipewire.h"
#include "portalfilepicker.h"
#include "touchpads.h"
#include "touchsource.h"

namespace tf::platform {

std::unique_ptr<Pad> openPad(FrameSink *sink) {
    const auto pads = Device::findTouchpads();
    return std::make_unique<TouchSource>(sink, pads.isEmpty() ? std::string() : pads.first().path.toStdString());
}

std::unique_ptr<Audio> openAudio(Studio *studio) { return std::make_unique<PipeWireAudio>(studio); }

std::unique_ptr<FilePicker> filePicker(std::function<void(const QString &)> onError) {
    auto picker = std::make_unique<PortalFilePicker>();
    picker->onError = std::move(onError);
    return picker;
}

int runSystemCommand(int argc, char **argv) {
    // `tenfold helper /dev/input/eventN` runs as root under pkexec: no GUI,
    // no Qt, no environment.
    if (argc == 3 && std::strcmp(argv[1], "helper") == 0)
        return runHelper(argv[2]);
    return -1;
}

int measureLatency(bool json) { return measurePipeWireLatency(json); }

}
