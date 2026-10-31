# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

TEMPLATE = app
TARGET = tst_watchproto
CONFIG += testcase
QT = core testlib
include(../../common.pri)

INCLUDEPATH += ../../core
LIBS += -L$$OUT_PWD/../../core -lcasiocore
PRE_TARGETDEPS += $$OUT_PWD/../../core/libcasiocore.a

SOURCES += tst_watchproto.cpp
