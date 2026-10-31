# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

# Files the package installs besides the program and the QML (which belongs to app/).
TEMPLATE = aux

models.path = /usr/share/sfos-casio-sync
models.files = models.json

license.path = /usr/share/licenses/sfos-casio-sync
license.files = $$PWD/../LICENSE

desktop.path = /usr/share/applications
desktop.files = sfos-casio-sync.desktop

dbusservice.path = /usr/share/dbus-1/services
dbusservice.files = dbus/it.michelebrami.sfos_casio_sync.service

systemdunits.path = /usr/lib/systemd/user
systemdunits.files = systemd/sfos-casio-sync-ping.service systemd/sfos-casio-sync-ping.timer

icon86.path = /usr/share/icons/hicolor/86x86/apps
icon86.files = icons/86x86/sfos-casio-sync.png
icon108.path = /usr/share/icons/hicolor/108x108/apps
icon108.files = icons/108x108/sfos-casio-sync.png
icon128.path = /usr/share/icons/hicolor/128x128/apps
icon128.files = icons/128x128/sfos-casio-sync.png
icon172.path = /usr/share/icons/hicolor/172x172/apps
icon172.files = icons/172x172/sfos-casio-sync.png

INSTALLS += models license desktop dbusservice systemdunits icon86 icon108 icon128 icon172

