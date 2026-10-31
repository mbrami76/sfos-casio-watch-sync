// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Text for times and intervals: local time, "Today 08:12", "Yesterday 08:12", otherwise "25 Oct
// 08:12"; the next sync is an estimate. English only (R-GEN-8); every string goes through tr().
#ifndef FORMATTER_H
#define FORMATTER_H

#include <QDateTime>
#include <QObject>
#include <QString>

namespace uibridge {

class Formatter : public QObject
{
    Q_OBJECT
public:
    explicit Formatter(QObject *parent = 0) : QObject(parent) {}

    // Pure forms, `now` given (tests). 0 / null time = never.
    static QString whenText(qlonglong epochSeconds, const QDateTime &now);
    static QString nextText(qlonglong epochSeconds, const QDateTime &now);
    // Cover and summary line: "Last sync <time>", "Not synced yet", "No watch enabled".
    static QString lastSyncLine(bool anyEnabled, qlonglong oldestLastSuccess, const QDateTime &now);
    static QString intervalLabel(int minutes);

    // For QML, with the current time.
    Q_INVOKABLE QString when(qlonglong epochSeconds) const;
    Q_INVOKABLE QString next(qlonglong epochSeconds) const;
    Q_INVOKABLE QString interval(int minutes) const { return intervalLabel(minutes); }
};

} // namespace uibridge

#endif // FORMATTER_H
