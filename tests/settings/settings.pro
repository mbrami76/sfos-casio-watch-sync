# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

TEMPLATE = app
TARGET = tst_settings
CONFIG += testcase
QT = core testlib
include(../../common.pri)

INCLUDEPATH += ../../core
LIBS += -L$$OUT_PWD/../../core -lcasiocore
PRE_TARGETDEPS += $$OUT_PWD/../../core/libcasiocore.a

SOURCES += tst_settings.cpp

# The real data file, checked by the tests.
DEFINES += MODELS_JSON=\\\"$$PWD/../../data/models.json\\\"
