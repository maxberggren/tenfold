QT += core gui qml quick quickcontrols2
# QRhi (rhi/qrhi.h), for reading the visualizer's offscreen frames back:
# Qt ships its headers with the private ones.
QT += gui-private

CONFIG += c++20 release
TARGET = tenfold
TEMPLATE = app

# The engine is the hot path: full optimisation and fast float maths (but
# NaN and infinity checks still mean something). Built here to play here, it
# is tuned for this machine's processor; `CONFIG+=portable` (what the
# released packages are built with, TENFOLD_PORTABLE=1 for bin/build) tunes
# it for any processor of the architecture instead, since a binary tuned for
# the build machine can die on an older one with an illegal instruction.
QMAKE_CXXFLAGS_RELEASE -= -O2
QMAKE_CXXFLAGS_RELEASE += -O3 -ffast-math -fno-finite-math-only
portable {
    linux:contains(QT_ARCH, x86_64): QMAKE_CXXFLAGS_RELEASE += -march=x86-64-v2 -mtune=generic
} else:!macx {
    contains(QT_ARCH, arm64): QMAKE_CXXFLAGS_RELEASE += -mcpu=native
    contains(QT_ARCH, x86_64): QMAKE_CXXFLAGS_RELEASE += -march=native
}

HEADERS += \
    src/backend.h \
    src/console.h \
    src/contacts.h \
    src/dsp.h \
    src/electro.h \
    src/fx.h \
    src/gong.h \
    src/hang.h \
    src/slitdrum.h \
    src/pipedrum.h \
    src/khishba.h \
    src/udu.h \
    src/tabla.h \
    src/cuica.h \
    src/offline.h \
    src/part.h \
    src/padprompt.h \
    src/platform.h \
    src/parts.h \
    src/phrase.h \
    src/session.h \
    src/songfile.h \
    src/studio.h \
    src/surface.h \
    src/theme.h \
    src/touch.h \
    src/vocal.h \
    src/visual.h \
    src/visualizer.h \
    src/recorder.h \
    src/scream.h \
    src/translator.h \
    src/tuning.h \
    src/tutorial.h \
    src/wav.h

# One file per part: each page's controls and engine.
PARTS = \
    src/drums.cpp \
    src/bass.cpp \
    src/chords.cpp \
    src/lead.cpp \
    src/arp.cpp \
    src/atmos.cpp \
    src/risers.cpp \
    src/vocal.cpp \
    src/arrange.cpp

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/console.cpp \
    src/contacts.cpp \
    src/offline.cpp \
    src/parts.cpp \
    src/session.cpp \
    src/songfile.cpp \
    src/studio.cpp \
    src/surface.cpp \
    src/theme.cpp \
    src/translator.cpp \
    src/tuning.cpp \
    src/tutorial.cpp \
    src/visual.cpp \
    src/visualizer.cpp \
    src/recorder.cpp \
    $$PARTS

# Part headers are not all listed above; this makes edits to them rebuild.
INCLUDEPATH += src
DEPENDPATH += src

RESOURCES += src/resources.qrc src/fonts.qrc

# What the system provides (platform.h): one folder per system. A port adds
# its folder and a scope here; the rest of tenfold doesn't change.
linux {
    QT += dbus
    CONFIG += link_pkgconfig
    PKGCONFIG += libpipewire-0.3
    INCLUDEPATH += src/linux
    HEADERS += \
        src/linux/helper.h \
        src/linux/pipewire.h \
        src/linux/portalfilepicker.h \
        src/linux/touchpads.h \
        src/linux/touchsource.h
    SOURCES += \
        src/linux/helper.cpp \
        src/linux/pipewire.cpp \
        src/linux/platform.cpp \
        src/linux/portalfilepicker.cpp \
        src/linux/touchpads.cpp \
        src/linux/touchsource.cpp
} else:macx {
    # An app bundle for macOS 12 and later, for Apple silicon and Intel
    # alike when Qt is (CONFIG+=portable builds both).
    INCLUDEPATH += src/macos
    HEADERS += \
        src/macos/coreaudio.h \
        src/macos/filepicker.h \
        src/macos/microphone.h \
        src/macos/trackpad.h
    SOURCES += src/macos/coreaudio.cpp
    OBJECTIVE_SOURCES += \
        src/macos/platform.mm \
        src/macos/trackpad.mm
    QMAKE_OBJECTIVE_CFLAGS += -fobjc-arc
    LIBS += -framework CoreAudio -framework AudioToolbox -framework CoreFoundation \
        -framework ApplicationServices -framework AppKit -framework AVFoundation \
        -framework UniformTypeIdentifiers
    QMAKE_INFO_PLIST = src/macos/Info.plist
    # Made from src/macos/tenfold-icon.svg (the Linux icon on macOS's
    # rounded square) at every size the Finder and the Dock ask for.
    ICON = src/macos/tenfold.icns
    QMAKE_MACOSX_DEPLOYMENT_TARGET = 12.0
    portable: QMAKE_APPLE_DEVICE_ARCHS = x86_64 arm64
} else {
    error("tenfold runs on Linux and macOS so far. A port implements src/platform.h in a folder of its own (see the README).")
}
