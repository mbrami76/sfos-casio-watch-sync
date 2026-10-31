// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Tests for scheduler. Times are built in UTC so the tests do not depend on the machine's
// time zone, except where the zone is the point.
#include "scheduler.h"
#include "wakescheduler.h"

#include <QSignalSpy>
#include <QtTest>

#include <stdlib.h>
#include <time.h>

using namespace scheduler;

namespace {

const qint64 kMin = 60, kHour = 3600, kDay = 86400;

QDateTime at(int y, int mo, int d, int h = 0, int mi = 0, int s = 0)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s), Qt::UTC);
}

QDateTime plus(const QDateTime &t, qint64 secs) { return t.addSecs(secs); }

WatchRunState ws(const QString &key, const QDateTime &success = QDateTime(),
                 const QDateTime &attempt = QDateTime(), int failures = 0, bool enabled = true)
{
    WatchRunState w;
    w.key = key;
    w.enabled = enabled;
    w.lastSuccess = success;
    w.lastAttempt = attempt;
    w.consecutiveFailures = failures;
    return w;
}

QStringList keys(const Plan &p)
{
    QStringList k;
    for (int i = 0; i < p.due.size(); ++i)
        k << p.due.at(i).key;
    return k;
}

class FixedClock : public ClockSource
{
public:
    QDateTime t;
    QDateTime now() const { return t; }
};

const QDateTime kNow = at(2026, 10, 25, 12, 0, 0);

} // namespace

class TestScheduler : public QObject
{
    Q_OBJECT
private slots:
    // ------------------------------------------------------------ decide
    void neverSyncedIsDueAtOnce()
    {
        Plan p = decide(kNow, kDay, QList<WatchRunState>() << ws("A"));
        QCOMPARE(keys(p), QStringList() << "A");
        QVERIFY(!p.nextWake.isValid());
    }
    void noWatchesNothingToDo()
    {
        Plan p = decide(kNow, kDay, QList<WatchRunState>());
        QVERIFY(p.due.isEmpty());
        QVERIFY(!p.nextWake.isValid());
    }
    void dueAtExactlyTheInterval()
    {
        QDateTime last = plus(kNow, -kDay);
        Plan p = decide(kNow, kDay, QList<WatchRunState>() << ws("A", last));
        QCOMPARE(keys(p), QStringList() << "A");           // >= interval
        p = decide(kNow.addSecs(-1), kDay, QList<WatchRunState>() << ws("A", last));
        QVERIFY(p.due.isEmpty());                          // one second early
        QCOMPARE(p.nextWake.toMSecsSinceEpoch(), kNow.toMSecsSinceEpoch());
        p = decide(kNow.addSecs(1), kDay, QList<WatchRunState>() << ws("A", last));
        QCOMPARE(keys(p), QStringList() << "A");
    }
    void notYetDue()
    {
        QDateTime last = plus(kNow, -6 * kHour);
        Plan p = decide(kNow, kDay, QList<WatchRunState>() << ws("A", last));
        QVERIFY(p.due.isEmpty());
        QCOMPARE(p.nextWake.toMSecsSinceEpoch(), plus(last, kDay).toMSecsSinceEpoch());
    }
    void intervalChange()
    {
        QDateTime last = plus(kNow, -6 * kHour);
        QList<WatchRunState> l = QList<WatchRunState>() << ws("A", last);
        QVERIFY(decide(kNow, kDay, l).due.isEmpty());
        QCOMPARE(keys(decide(kNow, 6 * kHour, l)), QStringList() << "A");  // shortened: due now
        QCOMPARE(keys(decide(kNow, kHour, l)), QStringList() << "A");
        Plan p = decide(kNow, 7 * kDay, l);                                // lengthened
        QVERIFY(p.due.isEmpty());
        QCOMPARE(p.nextWake.toMSecsSinceEpoch(), plus(last, 7 * kDay).toMSecsSinceEpoch());
    }
    void intervalFloor()
    {
        QDateTime last = plus(kNow, -10);
        QVERIFY(decide(kNow, 0, QList<WatchRunState>() << ws("A", last)).due.isEmpty());
        QVERIFY(decide(kNow, -5, QList<WatchRunState>() << ws("A", last)).due.isEmpty());
        Plan p = decide(kNow, 0, QList<WatchRunState>() << ws("A", last));
        QCOMPARE(p.nextWake.toMSecsSinceEpoch(), plus(last, 60).toMSecsSinceEpoch());
    }
    void backoffValues()
    {
        QCOMPARE(backoffSeconds(0), qint64(0));
        QCOMPARE(backoffSeconds(-1), qint64(0));
        QCOMPARE(backoffSeconds(1), 5 * kMin);
        QCOMPARE(backoffSeconds(2), 15 * kMin);
        QCOMPARE(backoffSeconds(3), kHour);
        QCOMPARE(backoffSeconds(4), kDay);
        QCOMPARE(backoffSeconds(99), kDay);
        // from the 4th failure: min(24 h, interval)
        QCOMPARE(backoffSeconds(4, 6 * kHour), 6 * kHour);
        QCOMPARE(backoffSeconds(99, 12 * kHour), 12 * kHour);
        QCOMPARE(backoffSeconds(4, kDay), kDay);
        QCOMPARE(backoffSeconds(4, 7 * kDay), kDay);
        QCOMPARE(backoffSeconds(3, 6 * kHour), kHour);        // first three steps unchanged
        QCOMPARE(backoffSeconds(1, 7 * kDay), 5 * kMin);
    }
    void longBackoffFollowsTheInterval()
    {
        const QDateTime attempt = plus(kNow, -kMin);
        const QDateTime last = plus(kNow, -3 * kDay);
        QList<WatchRunState> l;
        l << ws("A", last, attempt, 4);
        // interval 6 h: next attempt 6 h after the last attempt, not 24 h
        Plan p = decide(kNow, 6 * kHour, l);
        QVERIFY(p.due.isEmpty());
        QCOMPARE(p.nextWake.toMSecsSinceEpoch(), plus(attempt, 6 * kHour).toMSecsSinceEpoch());
        QCOMPARE(keys(decide(plus(attempt, 6 * kHour), 6 * kHour, l)), QStringList() << "A");
        // interval 7 d: capped at 24 h, as before
        p = decide(kNow, 7 * kDay, QList<WatchRunState>() << ws("A", plus(kNow, -8 * kDay), attempt, 5));
        QCOMPARE(p.nextWake.toMSecsSinceEpoch(), plus(attempt, kDay).toMSecsSinceEpoch());
    }
    void failuresFollowTheBackoffExactly_data()
    {
        QTest::addColumn<int>("failures");
        QTest::addColumn<qint64>("wait");
        QTest::newRow("1") << 1 << (5 * kMin);
        QTest::newRow("2") << 2 << (15 * kMin);
        QTest::newRow("3") << 3 << kHour;
        QTest::newRow("4") << 4 << kDay;
        QTest::newRow("5") << 5 << kDay;
    }
    void failuresFollowTheBackoffExactly()
    {
        QFETCH(int, failures);
        QFETCH(qint64, wait);
        QDateTime last = plus(kNow, -3 * kDay);          // long overdue
        QDateTime attempt = plus(kNow, -2 * kMin);       // failed 2 minutes ago
        QList<WatchRunState> l = QList<WatchRunState>() << ws("A", last, attempt, failures);
        Plan p = decide(kNow, kDay, l);
        QVERIFY(p.due.isEmpty());
        QCOMPARE(p.nextWake.toMSecsSinceEpoch(), plus(attempt, wait).toMSecsSinceEpoch());
        // one second before: still waiting; at the moment: due
        QVERIFY(decide(plus(attempt, wait - 1), kDay, l).due.size() == 0);
        QCOMPARE(keys(decide(plus(attempt, wait), kDay, l)), QStringList() << "A");
        QCOMPARE(keys(decide(plus(attempt, wait + 1), kDay, l)), QStringList() << "A");
    }
    void failureOnNeverSyncedWatch()
    {
        QDateTime attempt = plus(kNow, -1 * kMin);
        Plan p = decide(kNow, kDay, QList<WatchRunState>() << ws("A", QDateTime(), attempt, 1));
        QVERIFY(p.due.isEmpty());
        QCOMPARE(p.nextWake.toMSecsSinceEpoch(), plus(attempt, 5 * kMin).toMSecsSinceEpoch());
    }
    void successResetsTheBackoff()
    {
        // after a success the state has 0 failures and a fresh lastSuccess
        QDateTime success = plus(kNow, -10);
        Plan p = decide(kNow, kDay, QList<WatchRunState>()
                                        << ws("A", success, plus(kNow, -10), 0));
        QVERIFY(p.due.isEmpty());
        QCOMPARE(p.nextWake.toMSecsSinceEpoch(), plus(success, kDay).toMSecsSinceEpoch());
        // an old attempt time with 0 failures does not hold anything back
        p = decide(kNow, kDay, QList<WatchRunState>()
                                   << ws("A", plus(kNow, -2 * kDay), plus(kNow, -1), 0));
        QCOMPARE(keys(p), QStringList() << "A");
    }
    void backoffIsNotShorterThanTheInterval()
    {
        // 1 failure but the interval is not over: the watch was synced
        // recently, so the longer of the two rules applies
        QDateTime last = plus(kNow, -kHour);
        QDateTime attempt = plus(kNow, -10 * kMin);
        Plan p = decide(kNow, kDay, QList<WatchRunState>() << ws("A", last, attempt, 1));
        QVERIFY(p.due.isEmpty());
        QCOMPARE(p.nextWake.toMSecsSinceEpoch(), plus(last, kDay).toMSecsSinceEpoch());
    }
    void disabledWatchIsSkipped()
    {
        QList<WatchRunState> l;
        l << ws("A", QDateTime(), QDateTime(), 0, false)
          << ws("B", plus(kNow, -kHour), QDateTime(), 0, false);
        Plan p = decide(kNow, kDay, l);
        QVERIFY(p.due.isEmpty());
        QVERIFY(!p.nextWake.isValid());            // does not influence the wake-up either
    }
    void severalWatchesKeepListOrder()
    {
        QList<WatchRunState> l;
        l << ws("C", plus(kNow, -2 * kDay))                       // due
          << ws("A", plus(kNow, -kHour))                          // not due, wake in 23 h
          << ws("B")                                              // never synced: due
          << ws("D", plus(kNow, -3 * kDay), plus(kNow, -kMin), 2) // backing off 14 min more
          << ws("E", plus(kNow, -20 * kHour), QDateTime(), 0)     // not due, wake in 4 h
          << ws("F", plus(kNow, -5 * kDay), QDateTime(), 0, false);
        Plan p = decide(kNow, kDay, l);
        QCOMPARE(keys(p), QStringList() << "C" << "B");
        QCOMPARE(p.nextWake.toMSecsSinceEpoch(), plus(kNow, 14 * kMin).toMSecsSinceEpoch());
    }
    void nextWakeIsTheEarliestOfSeveral()
    {
        QList<WatchRunState> l;
        l << ws("A", plus(kNow, -kHour))
          << ws("B", plus(kNow, -20 * kHour))
          << ws("C", plus(kNow, -10 * kHour));
        Plan p = decide(kNow, kDay, l);
        QVERIFY(p.due.isEmpty());
        QCOMPARE(p.nextWake.toMSecsSinceEpoch(), plus(kNow, 4 * kHour).toMSecsSinceEpoch());
    }
    void clockJumpedBackwards()
    {
        // Success recorded "in the future": treated as just now, so the next
        // sync is one interval after now (not a year later, not at once).
        QDateTime future = plus(kNow, 365 * kDay);
        Plan p = decide(kNow, kDay, QList<WatchRunState>() << ws("A", future));
        QVERIFY(p.due.isEmpty());
        QCOMPARE(p.nextWake.toMSecsSinceEpoch(), plus(kNow, kDay).toMSecsSinceEpoch());
        // a small correction (5 s back) behaves the same
        p = decide(kNow, kDay, QList<WatchRunState>() << ws("A", plus(kNow, 5)));
        QVERIFY(p.due.isEmpty());
        QCOMPARE(p.nextWake.toMSecsSinceEpoch(), plus(kNow, kDay).toMSecsSinceEpoch());
        // attempt in the future: the back-off starts from now
        p = decide(kNow, kDay, QList<WatchRunState>()
                                   << ws("A", plus(kNow, -3 * kDay), plus(kNow, 2 * kHour), 1));
        QVERIFY(p.due.isEmpty());
        QCOMPARE(p.nextWake.toMSecsSinceEpoch(), plus(kNow, 5 * kMin).toMSecsSinceEpoch());
        // both in the future and failing: no burst
        p = decide(kNow, kDay, QList<WatchRunState>()
                                   << ws("A", plus(kNow, kDay), plus(kNow, kDay), 3));
        QVERIFY(p.due.isEmpty());
    }
    void clockJumpedForwards()
    {
        QDateTime last = plus(kNow, -3 * kHour);
        QList<WatchRunState> l = QList<WatchRunState>() << ws("A", last);
        QVERIFY(decide(kNow, kDay, l).due.isEmpty());
        QCOMPARE(keys(decide(plus(kNow, 30 * kDay), kDay, l)), QStringList() << "A"); // once
    }
    void unchangedStateIsStable()
    {
        // pure function: same inputs, same output, inputs untouched
        QList<WatchRunState> l = QList<WatchRunState>() << ws("A", plus(kNow, -kHour));
        Plan a = decide(kNow, kDay, l), b = decide(kNow, kDay, l);
        QCOMPARE(a.nextWake.toMSecsSinceEpoch(), b.nextWake.toMSecsSinceEpoch());
        QCOMPARE(l.at(0).consecutiveFailures, 0);
    }
    void phoneClockOnly_dstDayHasNoSpecialCase()
    {
        // Only instants matter. Europe/Rome leaves DST on 2026-10-25 03:00 -> 02:00
        // (that day has 25 hours): 24 h after the last success is exactly
        // 86400 s later, whatever the wall clock shows.
        QDateTime last = at(2026, 10, 24, 10, 0, 0);     // 12:00 local, CEST
        Plan p = decide(plus(last, kHour), kDay, QList<WatchRunState>() << ws("A", last));
        QCOMPARE(p.nextWake.toMSecsSinceEpoch(), plus(last, kDay).toMSecsSinceEpoch());
        // the same with local-time stamps, if the zone database knows Rome
        const QByteArray oldTz = qgetenv("TZ");
        qputenv("TZ", "Europe/Rome");
        tzset();
        QDateTime summer(QDate(2026, 7, 1), QTime(12, 0), Qt::LocalTime);
        if (summer.offsetFromUtc() == 7200) {
            QDateTime l1(QDate(2026, 10, 24), QTime(12, 0), Qt::LocalTime);
            Plan q = decide(l1.addSecs(kHour), kDay, QList<WatchRunState>() << ws("A", l1));
            QCOMPARE(q.nextWake.toMSecsSinceEpoch(), l1.toMSecsSinceEpoch() + kDay * 1000);
            QCOMPARE(q.nextWake.toTimeSpec(Qt::LocalTime).time(), QTime(11, 0)); // wall clock moved
        }
        if (oldTz.isNull()) unsetenv("TZ"); else qputenv("TZ", oldTz);
        tzset();
        if (summer.offsetFromUtc() != 7200)
            QSKIP("time zone Europe/Rome not available; instants checked above");
    }

    // ------------------------------------------------------------- clock
    void systemClockIsThePhoneClock()
    {
        SystemClock c;
        qint64 d = qAbs(c.now().toMSecsSinceEpoch() - QDateTime::currentMSecsSinceEpoch());
        QVERIFY(d < 1000);
    }

    // ------------------------------------------------------ wake policy
    void wakeRangePolicy_data()
    {
        QTest::addColumn<qint64>("delay");
        QTest::addColumn<int>("cap");
        QTest::addColumn<int>("min");
        QTest::addColumn<int>("max");
        QTest::newRow("past") << qint64(-100) << 14400 << 30 << 35;
        QTest::newRow("zero") << qint64(0) << 14400 << 30 << 35;
        QTest::newRow("30s") << qint64(30) << 14400 << 30 << 35;
        QTest::newRow("5min") << qint64(300) << 14400 << 300 << 330;
        QTest::newRow("1h") << qint64(3600) << 14400 << 3600 << 3960;
        QTest::newRow("4h") << qint64(14400) << 14400 << 14400 << 15300; // slack capped at 15 min
        QTest::newRow("1 day capped") << qint64(86400) << 14400 << 14400 << 15300;
        QTest::newRow("custom cap") << qint64(86400) << 3600 << 3600 << 3960;
        QTest::newRow("silly cap") << qint64(86400) << 0 << 30 << 35;
    }
    void wakeRangePolicy()
    {
        QFETCH(qint64, delay);
        QFETCH(int, cap);
        QFETCH(int, min);
        QFETCH(int, max);
        WakeRange r = wakeRangeFor(delay, cap);
        QCOMPARE(r.minSec, min);
        QCOMPARE(r.maxSec, max);
    }

    // ------------------------------------------- wrapper (host stub)
    void wrapperSchedulesWithinTheCap()
    {
        FixedClock clock; clock.t = kNow;
        BackgroundActivityWake w(&clock);
        QVERIFY(!w.isScheduled());
        QCOMPARE(w.maxSleepSeconds(), kDefaultMaxSleepSeconds);
        w.schedule(plus(kNow, 600));
        QVERIFY(w.isScheduled());
        QCOMPARE(w.lastRange().minSec, 600);
        QCOMPARE(w.lastRange().maxSec, 660);
        w.schedule(plus(kNow, 3 * kDay));            // replaces, capped
        QCOMPARE(w.lastRange().minSec, kDefaultMaxSleepSeconds);
        w.setMaxSleepSeconds(1800);
        w.schedule(plus(kNow, 3 * kDay));
        QCOMPARE(w.lastRange().minSec, 1800);
        w.setMaxSleepSeconds(1);                     // floor
        QCOMPARE(w.maxSleepSeconds(), kMinSleepSeconds);
        w.schedule(kNow.addSecs(-5));                // in the past: minimum delay, no tight loop
        QCOMPARE(w.lastRange().minSec, kMinSleepSeconds);
    }
    void wrapperWakeAndCancel()
    {
        FixedClock clock; clock.t = kNow;
        BackgroundActivityWake w(&clock);
        QSignalSpy spy(&w, SIGNAL(wake()));
        QVERIFY(!w.simulateWake());                  // nothing scheduled
        QCOMPARE(spy.count(), 0);
        w.schedule(plus(kNow, 600));
        QVERIFY(w.simulateWake());
        QCOMPARE(spy.count(), 1);
        QVERIFY(!w.isScheduled());                   // one request = one wake
        QVERIFY(!w.simulateWake());
        w.schedule(plus(kNow, 600));
        w.cancel();
        QVERIFY(!w.isScheduled());
        QVERIFY(!w.simulateWake());
        QCOMPARE(spy.count(), 1);
    }
    void wrapperThroughTheInterface()
    {
        FixedClock clock; clock.t = kNow;
        BackgroundActivityWake impl(&clock);
        WakeScheduler *w = &impl;                    // the service sees only the interface
        w->schedule(plus(kNow, 100));
        QVERIFY(w->isScheduled());
        w->cancel();
        QVERIFY(!w->isScheduled());
    }
    void serviceLoopSketch()
    {
        // decide -> sync due watches -> decide again -> schedule nextWake,
        // with the daily interval: reaches the next day in capped steps.
        FixedClock clock; clock.t = kNow;
        BackgroundActivityWake w(&clock);
        QList<WatchRunState> l = QList<WatchRunState>() << ws("A");
        Plan p = decide(clock.t, kDay, l);
        QCOMPARE(keys(p), QStringList() << "A");
        l[0].lastSuccess = clock.t;                  // sync succeeded
        p = decide(clock.t, kDay, l);
        QVERIFY(p.due.isEmpty());
        int wakes = 0;
        while (true) {
            w.schedule(p.nextWake);
            clock.t = clock.t.addSecs(w.lastRange().minSec);
            QVERIFY(w.simulateWake());
            ++wakes;
            p = decide(clock.t, kDay, l);
            if (!p.due.isEmpty())
                break;
            QVERIFY(wakes < 50);
        }
        QCOMPARE(wakes, 6);                          // 24 h in steps of at most 4 h
        QVERIFY(clock.t.toMSecsSinceEpoch() >= plus(kNow, kDay).toMSecsSinceEpoch());
    }
};

QTEST_APPLESS_MAIN(TestScheduler)

#include "tst_scheduler.moc"
