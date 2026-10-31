# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

# UI bridge: D-Bus client of the service, settings bridge, list models. No
# QtQuick, no Silica, so it builds and is tested in the host build.
TEMPLATE = lib
CONFIG += staticlib
TARGET = casiobridge
QT = core dbus
include(../../common.pri)

INCLUDEPATH += ../../core
include(bridge.pri)
