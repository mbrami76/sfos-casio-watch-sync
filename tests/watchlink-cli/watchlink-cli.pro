# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

# On-phone test program for watchlink. Talks to BlueZ on the system bus; no Qt GUI, no network.
TEMPLATE = app
TARGET = watchlink-cli
QT = core dbus
include(../../common.pri)

INCLUDEPATH += ../../core
LIBS += -L$$OUT_PWD/../../core -lcasiocore
PRE_TARGETDEPS += $$OUT_PWD/../../core/libcasiocore.a

SOURCES += main.cpp

!hostbuild {
    CONFIG += link_pkgconfig
    PKGCONFIG += keepalive
}
