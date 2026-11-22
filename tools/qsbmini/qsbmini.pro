# A stand-in for Qt's qsb, for machines without qt6-shadertools: bakes a
# Vulkan-style GLSL 440 fragment shader into a .qsb with the GLSL variants
# OpenGL needs (and SPIR-V through glslc when it is there). bin/shaders
# builds and runs it; it is never part of the app.
QT += core gui gui-private
CONFIG += c++20 console
CONFIG -= app_bundle
TARGET = qsbmini
TEMPLATE = app
SOURCES += main.cpp
