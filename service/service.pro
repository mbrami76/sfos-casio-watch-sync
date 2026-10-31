# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

# Headless logic of `sfos-casio-sync --service`.
TEMPLATE = lib
CONFIG += staticlib link_pkgconfig
TARGET = casioservice
QT = core dbus gui
include(../common.pri)

INCLUDEPATH += ../core
PKGCONFIG += dbus-1 nemonotifications-qt5

HEADERS += \
    serviceiface.h \
    servicelog.h \
    servicecore.h \
    servicebus.h \
    servicemain.h \
    workerrunner.h \
    monitorsource.h \
    nemopublisher.h

SOURCES += \
    servicelog.cpp \
    servicecore.cpp \
    servicebus.cpp \
    servicemain.cpp \
    workerrunner.cpp \
    monitorsource.cpp \
    nemopublisher.cpp

# Same switch as core/core.pro: libkeepalive on the device, a stub on the host.
!hostbuild {
    PKGCONFIG += keepalive
    DEFINES += HAVE_KEEPALIVE
}
