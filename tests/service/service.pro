# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

TEMPLATE = app
TARGET = tst_service
CONFIG += testcase link_pkgconfig
QT = core dbus gui testlib
PKGCONFIG += dbus-1 nemonotifications-qt5
include(../../common.pri)

INCLUDEPATH += ../../core ../../service
LIBS += -L$$OUT_PWD/../../service -lcasioservice -L$$OUT_PWD/../../core -lcasiocore
PRE_TARGETDEPS += $$OUT_PWD/../../service/libcasioservice.a $$OUT_PWD/../../core/libcasiocore.a

SOURCES += tst_service.cpp

!hostbuild {
    PKGCONFIG += keepalive
}
