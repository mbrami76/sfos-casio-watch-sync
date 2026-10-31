// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "scheduler.h"

namespace scheduler {

namespace {

const qint64 kDay = 24 * 60 * 60;

} // namespace

qint64 backoffSeconds(int failures, qint64 intervalSeconds)
{
    if (failures <= 0)
        return 0;
    switch (failures) {
    case 1: return 5 * 60;
    case 2: return 15 * 60;
    case 3: return 60 * 60;
    default: return intervalSeconds < kDay ? intervalSeconds : kDay;
    }
}

Plan decide(const QDateTime &now, qint64 intervalSeconds, const QList<WatchRunState> &watches)
{
    Plan plan;
    const qint64 n = now.toMSecsSinceEpoch();
    const qint64 interval = (intervalSeconds < kMinIntervalSeconds ? kMinIntervalSeconds
                                                                    : intervalSeconds) * 1000;
    bool haveNext = false;
    qint64 next = 0;

    for (int i = 0; i < watches.size(); ++i) {
        const WatchRunState &w = watches.at(i);
        if (!w.enabled)
            continue;

        qint64 at = n;  // earliest moment this watch may be synced
        if (w.lastSuccess.isValid()) {
            const qint64 anchor = qMin(w.lastSuccess.toMSecsSinceEpoch(), n);
            at = anchor + interval;
        }
        if (w.consecutiveFailures > 0 && w.lastAttempt.isValid()) {
            const qint64 anchor = qMin(w.lastAttempt.toMSecsSinceEpoch(), n);
            at = qMax(at, anchor + backoffSeconds(w.consecutiveFailures, interval / 1000) * 1000);
        }

        if (at <= n) {
            WatchDue d;
            d.key = w.key;
            plan.due << d;
        } else if (!haveNext || at < next) {
            haveNext = true;
            next = at;
        }
    }
    if (haveNext)
        plan.nextWake = QDateTime::fromMSecsSinceEpoch(next, Qt::UTC);
    return plan;
}

WakeRange wakeRangeFor(qint64 delaySeconds, int maxSleepSeconds)
{
    qint64 cap = maxSleepSeconds < kMinSleepSeconds ? kMinSleepSeconds : maxSleepSeconds;
    qint64 d = delaySeconds;
    if (d < kMinSleepSeconds)
        d = kMinSleepSeconds;
    if (d > cap)
        d = cap;
    qint64 slack = d / 10;
    if (slack < 5)
        slack = 5;
    if (slack > 15 * 60)
        slack = 15 * 60;
    WakeRange r;
    r.minSec = int(d);
    r.maxSec = int(d + slack);
    return r;
}

} // namespace scheduler
