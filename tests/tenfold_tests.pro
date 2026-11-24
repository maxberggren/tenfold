QT += core gui gui-private testlib
CONFIG += c++20 console testcase release link_pkgconfig
TARGET = tenfold_tests
TEMPLATE = app
# A plain binary on macOS too, not an app bundle.
CONFIG -= app_bundle

QMAKE_CXXFLAGS_RELEASE -= -O2
QMAKE_CXXFLAGS_RELEASE += -O3 -ffast-math -fno-finite-math-only

INCLUDEPATH += ../src
# The visualizer's tests read its shaders and their baked .qsb from the source.
DEFINES += TENFOLD_SRC=\\\"$$PWD/../src\\\"
DEPENDPATH += ../src

# Everything but the window: the studio, the session, every part, the
# offline player.
HEADERS += testkit.h \
    ../src/console.h ../src/contacts.h ../src/dsp.h ../src/electro.h ../src/fx.h ../src/gong.h ../src/trombamarina.h ../src/hang.h ../src/tabla.h ../src/udu.h ../src/slitdrum.h ../src/pipedrum.h ../src/khishba.h ../src/cuica.h ../src/offline.h ../src/part.h \
    ../src/parts.h ../src/phrase.h ../src/session.h ../src/songfile.h ../src/studio.h ../src/touch.h \
    ../src/padprompt.h ../src/platform.h ../src/tuning.h ../src/tutorial.h ../src/vocal.h ../src/visual.h ../src/recorder.h ../src/scream.h ../src/wav.h

PARTS = ../src/drums.cpp ../src/bass.cpp ../src/chords.cpp ../src/lead.cpp ../src/arp.cpp \
        ../src/atmos.cpp ../src/risers.cpp ../src/vocal.cpp ../src/arrange.cpp

SOURCES += main.cpp framework_tests.cpp \
    drums_tests.cpp hang_tests.cpp tabla_tests.cpp udu_tests.cpp khishba_tests.cpp slitdrum_tests.cpp pipedrum_tests.cpp cuica_tests.cpp scream_tests.cpp gong_tests.cpp trombamarina_tests.cpp bass_tests.cpp chords_tests.cpp lead_tests.cpp arp_tests.cpp \
    atmos_tests.cpp risers_tests.cpp vocal_tests.cpp arrange_tests.cpp tutorial_tests.cpp song_tests.cpp visual_tests.cpp \
    ../src/console.cpp ../src/contacts.cpp ../src/offline.cpp ../src/parts.cpp ../src/session.cpp ../src/songfile.cpp ../src/studio.cpp \
    ../src/tuning.cpp ../src/tutorial.cpp ../src/visual.cpp ../src/recorder.cpp \
    $$PARTS

# The typeface the pages lay out their buttons with, as in the app.
RESOURCES += ../src/fonts.qrc

# The one piece of the platform the tests touch: how the Linux trackpad
# says what it's missing.
linux {
    INCLUDEPATH += ../src/linux
    HEADERS += ../src/linux/touchsource.h
    SOURCES += ../src/linux/touchsource.cpp
}
