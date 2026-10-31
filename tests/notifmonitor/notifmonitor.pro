# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

TEMPLATE = app
TARGET = tst_notifmonitor
CONFIG += testcase link_pkgconfig
QT = core dbus testlib
PKGCONFIG += dbus-1
include(../../common.pri)

INCLUDEPATH += ../../core
LIBS += -L$$OUT_PWD/../../core -lcasiocore
PRE_TARGETDEPS += $$OUT_PWD/../../core/libcasiocore.a

SOURCES += tst_notifmonitor.cpp
