QT += core gui quick quickcontrols2 testlib dbus svg
CONFIG += c++17 testcase
TARGET = chalk_tests
TEMPLATE = app

INCLUDEPATH += ../src

HEADERS += \
    ../src/backend.h \
    ../src/document.h \
    ../src/exporter.h \
    ../src/filepicker.h \
    ../src/renderer.h \
    ../src/theme.h \
    ../src/translator.h

SOURCES += \
    chalk_tests.cpp \
    ../src/backend.cpp \
    ../src/document.cpp \
    ../src/exporter.cpp \
    ../src/renderer.cpp \
    ../src/theme.cpp \
    ../src/translator.cpp
DEFINES += SOURCE_DIR=\\\"$$PWD/..\\\"
