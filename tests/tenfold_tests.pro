QT += core testlib
QT -= gui

CONFIG += c++20 console testcase release
TARGET = tenfold_tests
TEMPLATE = app

QMAKE_CXXFLAGS_RELEASE -= -O2
QMAKE_CXXFLAGS_RELEASE += -O3 -ffast-math -fno-finite-math-only

INCLUDEPATH += ../src

HEADERS += ../src/engine.h ../src/gesture.h ../src/tuning.h ../src/offline.h ../src/dsp.h ../src/fx.h ../src/voices.h
SOURCES += tenfold_tests.cpp \
    ../src/engine.cpp \
    ../src/gesture.cpp \
    ../src/tuning.cpp \
    ../src/offline.cpp \
    ../src/demo.cpp
