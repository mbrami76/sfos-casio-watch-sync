# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

# The program and the UI bridge. The bridge is a static library that builds everywhere; the program
# is the binary (service and UI).
TEMPLATE = subdirs
SUBDIRS = bridge program
program.depends = bridge

# QML pages, images and silhouettes. The desktop file, icons, D-Bus
# and systemd files belong to packaging.
qml.files = qml
qml.path = /usr/share/sfos-casio-sync
images.files = images
images.path = /usr/share/sfos-casio-sync
INSTALLS += qml images
