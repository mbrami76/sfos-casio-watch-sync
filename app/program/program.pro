# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

# The one binary: `--service` runs the headless service, without it the UI starts. The UI
# (SailfishApp, QtQuick, Silica) is compiled only without CONFIG+=hostbuild; the host build keeps
# the service mode and prints a message instead of the UI.
TEMPLATE = app
TARGET = sfos-casio-sync
CONFIG += link_pkgconfig
QT = core dbus gui
include(../../common.pri)

INCLUDEPATH += ../../core ../../service ../bridge
LIBS += -L$$OUT_PWD/../bridge -lcasiobridge \
        -L$$OUT_PWD/../../service -lcasioservice \
        -L$$OUT_PWD/../../core -lcasiocore
PRE_TARGETDEPS += $$OUT_PWD/../bridge/libcasiobridge.a \
                  $$OUT_PWD/../../service/libcasioservice.a \
                  $$OUT_PWD/../../core/libcasiocore.a
PKGCONFIG += dbus-1 nemonotifications-qt5

SOURCES += main.cpp

!hostbuild {
    QT += quick qml
    PKGCONFIG += keepalive sailfishapp
    DEFINES += WITH_UI
    QMAKE_RPATHDIR += /usr/share/$${TARGET}/lib
}

target.path = /usr/bin
INSTALLS += target
