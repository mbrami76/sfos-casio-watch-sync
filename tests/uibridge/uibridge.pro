# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

TEMPLATE = app
TARGET = tst_uibridge
CONFIG += testcase link_pkgconfig
QT = core dbus testlib
include(../../common.pri)

# The sources of the bridge are compiled in (the same list as the static
# library app/bridge): the test does not depend on the build order of the
# top-level project.
INCLUDEPATH += ../../core
include(../../app/bridge/bridge.pri)
LIBS += -L$$OUT_PWD/../../core -lcasiocore
PRE_TARGETDEPS += $$OUT_PWD/../../core/libcasiocore.a
PKGCONFIG += dbus-1

SOURCES += tst_uibridge.cpp

# Real data files checked by the tests.
DEFINES += MODELS_JSON=\\\"$$PWD/../../data/models.json\\\"
DEFINES += SILHOUETTES_DIR=\\\"$$PWD/../../app/images/silhouettes\\\"
DEFINES += QML_DIR=\\\"$$PWD/../../app/qml\\\"

!hostbuild {
    PKGCONFIG += keepalive
}
