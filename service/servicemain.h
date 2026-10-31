// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Entry of `sfos-casio-sync --service`: builds the real objects, takes the
// D-Bus name and runs the event loop.
#ifndef SERVICEMAIN_H
#define SERVICEMAIN_H

#include <QString>

namespace service {

// Needs a QCoreApplication. `modelsFile`, if not empty, is read before the
// installed models.json (hand runs without a package). Returns the exit code:
// 0 after a clean quit, 1 if the name is taken or the session bus is missing.
int runService(const QString &modelsFile);

} // namespace service

#endif // SERVICEMAIN_H
