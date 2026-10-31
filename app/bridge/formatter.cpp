// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "formatter.h"

#include <QLocale>

namespace uibridge {

// Month names in English, whatever the system locale (V1 is English only).
static QString dayAndTime(const QDateTime &t)
{
    return QLocale(QLocale::English).toString(t, QStringLiteral("d MMM HH:mm"));
}

QString Formatter::whenText(qlonglong epochSeconds, const QDateTime &now)
{
    if (epochSeconds <= 0)
        return tr("Never");
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(epochSeconds * 1000).toLocalTime();
    const QDate today = now.toLocalTime().date();
    const QString hm = t.toString(QStringLiteral("HH:mm"));
    if (t.date() == today)
        return tr("Today %1").arg(hm);
    if (t.date() == today.addDays(-1))
        return tr("Yesterday %1").arg(hm);
    return dayAndTime(t);
}

QString Formatter::nextText(qlonglong epochSeconds, const QDateTime &now)
{
    if (epochSeconds <= 0)
        return tr("not scheduled");
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(epochSeconds * 1000).toLocalTime();
    if (t <= now)
        return tr("due now");
    const QDate today = now.toLocalTime().date();
    const QString hm = t.toString(QStringLiteral("HH:mm"));
    if (t.date() == today)
        return tr("about %1 today").arg(hm);
    if (t.date() == today.addDays(1))
        return tr("about %1 tomorrow").arg(hm);
    return tr("about %1").arg(dayAndTime(t));
}

QString Formatter::lastSyncLine(bool anyEnabled, qlonglong oldestLastSuccess, const QDateTime &now)
{
    if (!anyEnabled)
        return tr("No watch enabled");
    if (oldestLastSuccess <= 0)
        return tr("Not synced yet");
    return tr("Last sync %1").arg(whenText(oldestLastSuccess, now));
}

QString Formatter::intervalLabel(int minutes)
{
    // The list of settings::allowedIntervalMinutes(): 6 h, 12 h, 24 h, 2 d, 7 d, 2 w, 30 d.
    switch (minutes) {
    case 360: return tr("6 hours");
    case 720: return tr("12 hours");
    case 1440: return tr("24 hours");
    case 2880: return tr("2 days");
    case 10080: return tr("7 days");
    case 20160: return tr("2 weeks");
    case 43200: return tr("1 month (30 days)");
    }
    return tr("%1 minutes").arg(minutes);
}

QString Formatter::when(qlonglong epochSeconds) const
{
    return whenText(epochSeconds, QDateTime::currentDateTime());
}

QString Formatter::next(qlonglong epochSeconds) const
{
    return nextText(epochSeconds, QDateTime::currentDateTime());
}

} // namespace uibridge
