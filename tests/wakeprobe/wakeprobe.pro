# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

# On-phone probe for the wake-ups. Needs libkeepalive, so it is only built outside the host test
# build. Instructions:.
TEMPLATE = app
TARGET = wakeprobe
QT = core
include(../../common.pri)

INCLUDEPATH += ../../core
LIBS += -L$$OUT_PWD/../../core -lcasiocore
PRE_TARGETDEPS += $$OUT_PWD/../../core/libcasiocore.a
CONFIG += link_pkgconfig
PKGCONFIG += keepalive

SOURCES += main.cpp
