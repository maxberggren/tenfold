QT += core gui qml quick quickcontrols2

CONFIG += c++20 release link_pkgconfig
PKGCONFIG += libpipewire-0.3
TARGET = tenfold
TEMPLATE = app

# The engine is the hot path: full optimisation, fast float maths (but NaN
# and infinity checks still mean something), tuned for the machine it's
# built on, which is the machine it plays on.
QMAKE_CXXFLAGS_RELEASE -= -O2
QMAKE_CXXFLAGS_RELEASE += -O3 -ffast-math -fno-finite-math-only
contains(QT_ARCH, arm64): QMAKE_CXXFLAGS_RELEASE += -mcpu=native
contains(QT_ARCH, x86_64): QMAKE_CXXFLAGS_RELEASE += -march=native

HEADERS += \
    src/audio.h \
    src/backend.h \
    src/console.h \
    src/device.h \
    src/dsp.h \
    src/electro.h \
    src/fx.h \
    src/helper.h \
    src/offline.h \
    src/part.h \
    src/parts.h \
    src/phrase.h \
    src/session.h \
    src/starter.h \
    src/studio.h \
    src/surface.h \
    src/theme.h \
    src/touch.h \
    src/touchsource.h \
    src/translator.h \
    src/tuning.h \
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
    src/arrange.cpp

SOURCES += \
    src/main.cpp \
    src/audio.cpp \
    src/backend.cpp \
    src/console.cpp \
    src/device.cpp \
    src/helper.cpp \
    src/offline.cpp \
    src/parts.cpp \
    src/session.cpp \
    src/studio.cpp \
    src/surface.cpp \
    src/theme.cpp \
    src/touchsource.cpp \
    src/translator.cpp \
    src/tuning.cpp \
    $$PARTS

RESOURCES += src/resources.qrc
