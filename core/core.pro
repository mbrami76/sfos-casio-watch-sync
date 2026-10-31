# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

TEMPLATE = lib
CONFIG += staticlib
TARGET = casiocore
QT = core dbus
include(../common.pri)

HEADERS += \
    watchproto.h \
    modeltable.h \
    notiffilter.h \
    settings.h \
    scheduler.h \
    wakescheduler.h \
    watchlink.h \
    bluezadapter.h \
    notifmonitor.h

SOURCES += \
    watchproto.cpp \
    modeltable.cpp \
    notiffilter.cpp \
    settings.cpp \
    scheduler.cpp \
    wakescheduler.cpp \
    watchlink.cpp \
    bluezadapter.cpp \
    notifmonitor.cpp

# notifmonitor talks to the bus with libdbus directly (see notifmonitor.h).
CONFIG += link_pkgconfig
PKGCONFIG += dbus-1

# Wake-ups use libkeepalive on the device (and the emulator); the host test
# build (CONFIG+=hostbuild) compiles a stub instead. Targets linking this
# library on the device add PKGCONFIG += keepalive as well.
!hostbuild {
    PKGCONFIG += keepalive
    DEFINES += HAVE_KEEPALIVE
}
