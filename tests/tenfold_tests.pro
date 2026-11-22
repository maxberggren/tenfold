QT += core gui testlib
CONFIG += c++20 console testcase release link_pkgconfig
TARGET = tenfold_tests
TEMPLATE = app

QMAKE_CXXFLAGS_RELEASE -= -O2
QMAKE_CXXFLAGS_RELEASE += -O3 -ffast-math -fno-finite-math-only

INCLUDEPATH += ../src
DEPENDPATH += ../src

# Everything but the window: the studio, the session, every part, the
# offline player.
HEADERS += testkit.h \
    ../src/console.h ../src/dsp.h ../src/electro.h ../src/fx.h ../src/offline.h ../src/part.h \
    ../src/parts.h ../src/phrase.h ../src/session.h ../src/songfile.h ../src/studio.h ../src/touch.h \
    ../src/platform.h ../src/tuning.h ../src/tutorial.h ../src/vocal.h ../src/wav.h

PARTS = ../src/drums.cpp ../src/bass.cpp ../src/chords.cpp ../src/lead.cpp ../src/arp.cpp \
        ../src/atmos.cpp ../src/risers.cpp ../src/vocal.cpp ../src/arrange.cpp

SOURCES += main.cpp framework_tests.cpp \
    drums_tests.cpp bass_tests.cpp chords_tests.cpp lead_tests.cpp arp_tests.cpp \
    atmos_tests.cpp risers_tests.cpp vocal_tests.cpp arrange_tests.cpp tutorial_tests.cpp song_tests.cpp \
    ../src/console.cpp ../src/offline.cpp ../src/parts.cpp ../src/session.cpp ../src/songfile.cpp ../src/studio.cpp \
    ../src/tuning.cpp ../src/tutorial.cpp \
    $$PARTS

# The one piece of the platform the tests touch: how the Linux trackpad
# says what it's missing.
linux {
    INCLUDEPATH += ../src/linux
    HEADERS += ../src/linux/touchsource.h
    SOURCES += ../src/linux/touchsource.cpp
}
