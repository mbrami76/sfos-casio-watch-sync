// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Debug log of the service: off by default, metadata only (times, app names, counts, error codes),
// never notification text. Thread-safe: the worker thread logs through the same functions.
#ifndef SERVICELOG_H
#define SERVICELOG_H

#include <QString>

namespace service {

void setDebugLog(bool on);
bool debugLogOn();
// Written to stderr with a time stamp when the debug log is on.
void debugLog(const QString &line);

} // namespace service

#endif // SERVICELOG_H
