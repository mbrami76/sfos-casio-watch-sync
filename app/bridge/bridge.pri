# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

# Sources of the UI bridge. Shared by the static library (bridge.pro) and by the host test, so the
# test does not depend on the order in which the top-level project builds its subdirectories.
BRIDGE_DIR = $$PWD

HEADERS += \
    $$BRIDGE_DIR/appinfo.h \
    $$BRIDGE_DIR/appsmodel.h \
    $$BRIDGE_DIR/formatter.h \
    $$BRIDGE_DIR/instanceguard.h \
    $$BRIDGE_DIR/serviceclient.h \
    $$BRIDGE_DIR/settingsbridge.h \
    $$BRIDGE_DIR/silhouettecatalog.h \
    $$BRIDGE_DIR/uibridge.h \
    $$BRIDGE_DIR/watchesmodel.h

SOURCES += \
    $$BRIDGE_DIR/appinfo.cpp \
    $$BRIDGE_DIR/appsmodel.cpp \
    $$BRIDGE_DIR/formatter.cpp \
    $$BRIDGE_DIR/instanceguard.cpp \
    $$BRIDGE_DIR/serviceclient.cpp \
    $$BRIDGE_DIR/settingsbridge.cpp \
    $$BRIDGE_DIR/silhouettecatalog.cpp \
    $$BRIDGE_DIR/uibridge.cpp \
    $$BRIDGE_DIR/watchesmodel.cpp

INCLUDEPATH += $$BRIDGE_DIR

# Version shown on the About page; the package version is set by the packaging.
isEmpty(CWS_VERSION): CWS_VERSION = 1.0.0
DEFINES += CWS_VERSION=\\\"$$CWS_VERSION\\\"

# Project address shown on the About page; the package passes its URL field.
isEmpty(CWS_URL): CWS_URL = https://github.com/mbrami76/sfos-casio-watch-sync
DEFINES += CWS_URL=\\\"$$CWS_URL\\\"
