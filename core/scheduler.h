// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// scheduler: which watches need a time sync at a wake-up, and the policy of the wake-ups
// themselves. Pure logic here; the BackgroundActivity wrapper is in wakescheduler.h. Requirements:
// R-TIME-8, R-TIME-9, R-TIME-12;
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include <QDateTime>
#include <QList>
#include <QString>

namespace scheduler {

// What the service holds per watch (settings::WatchState + WatchEntry::enabled).
struct WatchRunState {
    QString key;
    bool enabled;
    QDateTime lastSuccess;       // null if never synced
    QDateTime lastAttempt;       // null if never tried
    int consecutiveFailures;     // 0 after a success
    WatchRunState() : enabled(true), consecutiveFailures(0) {}
};

struct WatchDue {
    QString key;
};

struct Plan {
    QList<WatchDue> due;         // watches to sync now, in list order
    QDateTime nextWake;          // invalid if no enabled watch is waiting
};

// The only time source is the phone clock (R-TIME-12); tests supply their own.
class ClockSource
{
public:
    virtual ~ClockSource() {}
    virtual QDateTime now() const = 0;
};

class SystemClock : public ClockSource
{
public:
    QDateTime now() const { return QDateTime::currentDateTime(); }
};

// Back-off after a failed attempt: 1st failure 5 min, 2nd 15 min, 3rd 1 h, from the 4th on min(24
// h, interval) after the last attempt, so a 6 h interval is not held back for a day. 0 failures ->
// 0. `intervalSeconds` is the sync interval.
qint64 backoffSeconds(int consecutiveFailures, qint64 intervalSeconds = 24 * 60 * 60);

// Shortest sync interval honoured, whatever the caller passes (settings
// enforce 15 min; this only guards against a tight loop).
const qint64 kMinIntervalSeconds = 60;

// A watch is due when it is enabled and (never synced, or the interval has
// elapsed since the last success) and the back-off since the last attempt has
// elapsed. Times in the future of `now` (the phone clock was set back) are
// treated as `now`, so a clock jump never causes an immediate burst and never
// delays a sync by more than one interval. Time zone and DST play no part:
// all arithmetic is on instants (seconds since the epoch).
//
// `nextWake` is the earliest moment a watch that is not due now becomes due.
// Watches that are due now are not counted: after attempting them, update the
// states and call decide() again to get the next wake-up.
Plan decide(const QDateTime &now, qint64 intervalSeconds, const QList<WatchRunState> &watches);

// Wake-up policy. A wake is requested as a delay range [minSec, maxSec] (the
// BackgroundActivity API): the delay is at least kMinSleepSeconds (never a
// tight loop), at most maxSleepSeconds, and the range is widened by 10 % of
// the delay (5 s .. 15 min) so that the system can batch wake-ups.
struct WakeRange {
    int minSec, maxSec;
};
const int kMinSleepSeconds = 30;
const int kDefaultMaxSleepSeconds = 4 * 60 * 60;  // [assumption] unmeasured,
WakeRange wakeRangeFor(qint64 delaySeconds, int maxSleepSeconds = kDefaultMaxSleepSeconds);

} // namespace scheduler

#endif // SCHEDULER_H
