QT += core gui qml quick quickcontrols2 dbus svg

CONFIG += c++17 release
TARGET = chalk
TEMPLATE = app

HEADERS += \
    src/backend.h \
    src/canvasview.h \
    src/cli.h \
    src/document.h \
    src/exporter.h \
    src/filepicker.h \
    src/portalfilepicker.h \
    src/renderer.h \
    src/theme.h \
    src/translator.h

SOURCES += \
    src/main.cpp \
    src/backend.cpp \
    src/canvasview.cpp \
    src/cli.cpp \
    src/document.cpp \
    src/exporter.cpp \
    src/portalfilepicker.cpp \
    src/renderer.cpp \
    src/theme.cpp \
    src/translator.cpp

RESOURCES += src/resources.qrc
