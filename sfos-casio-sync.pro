# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Michele Brami

# Top-level project: core library, service, app, data files and tests.
TEMPLATE = subdirs
SUBDIRS = core service app data tests
service.depends = core
app.depends = service core
tests.depends = core service
