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
    src/device.h \
    src/dsp.h \
    src/engine.h \
    src/electro.h \
    src/enginesink.h \
    src/fx.h \
    src/gesture.h \
    src/helper.h \
    src/offline.h \
    src/surface.h \
    src/theme.h \
    src/touchsource.h \
    src/translator.h \
    src/tuning.h \
    src/tutorial.h \
    src/voices.h \
    src/wav.h

SOURCES += \
    src/main.cpp \
    src/audio.cpp \
    src/backend.cpp \
    src/demo.cpp \
    src/device.cpp \
    src/engine.cpp \
    src/gesture.cpp \
    src/helper.cpp \
    src/offline.cpp \
    src/surface.cpp \
    src/theme.cpp \
    src/touchsource.cpp \
    src/translator.cpp \
    src/tuning.cpp \
    src/tutorial.cpp

RESOURCES += src/resources.qrc
