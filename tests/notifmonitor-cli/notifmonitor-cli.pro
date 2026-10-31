# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

# On-phone test program for notifmonitor. Needs the session bus of the user, run outside Sailjail;
# no network.
TEMPLATE = app
TARGET = notifmonitor-cli
CONFIG += link_pkgconfig
QT = core dbus
PKGCONFIG += dbus-1
include(../../common.pri)

INCLUDEPATH += ../../core
LIBS += -L$$OUT_PWD/../../core -lcasiocore
PRE_TARGETDEPS += $$OUT_PWD/../../core/libcasiocore.a

SOURCES += main.cpp
