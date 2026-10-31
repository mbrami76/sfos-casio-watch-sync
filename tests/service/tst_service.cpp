// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Host tests of the service wiring: ServiceCore with fakes for the worker, the monitor, the
// wake-ups, the clock, the resident notification and the awake hold; the D-Bus API and the second
// instance against a private dbus-daemon (skipped when it cannot be started).
#include "notifmonitor.h"
#include "servicebus.h"
#include "servicecore.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>

using namespace service;

namespace {

const char kModels[] =
    "{\"version\":1,\"models\":["
    "{\"id\":\"dw-h5600\",\"label\":\"Casio DW-H5600\",\"family\":\"h5600\","
    "\"status\":\"supported\",\"defaultSilhouette\":\"dw-h5600\",\"namePatterns\":[\"CASIO DW-H5600\"],\"params\":{\"dowEncoding\":\"sun0\"}},"
    "{\"id\":\"gw-b5600\",\"label\":\"Casio GW-B5600\",\"family\":\"b5600\","
    "\"status\":\"untested\",\"namePatterns\":[\"CASIO GW-B5600\"],\"params\":{\"dowEncoding\":\"sun0\"}}"
    "]}";

class FakeClock : public scheduler::ClockSource
{
public:
    FakeClock() : t(QDateTime(QDate(2026, 10, 26), QTime(8, 0, 0), Qt::UTC)) {}
    QDateTime now() const { return t; }
    QDateTime t;
};

class FakeWake : public scheduler::WakeScheduler
{
public:
    FakeWake() : scheduled(false), schedules(0), cancels(0), count(0), maxCount(0), acquires(0) {}
    void schedule(const QDateTime &at)
    {
        // like the wrapper: a new request replaces a pending one
        when = at;
        scheduled = true;
        ++schedules;
    }
    void cancel() { scheduled = false; ++cancels; }
    bool isScheduled() const { return scheduled; }
    void hold() { ++count; ++acquires; if (count > maxCount) maxCount = count; }
    void release() { --count; }
    void fire() { scheduled = false; emit wake(); }
    bool scheduled;
    QDateTime when;
    int schedules, cancels;
    int count, maxCount, acquires;       // the stay-awake hold
};

class FakeRunner : public SessionRunner
{
public:
    FakeRunner() : lists(0) {}
    void submit(const SessionJob &job) { jobs.append(job); }
    void listWatches() { ++lists; }
    void finish(const QList<watchlink::SessionResult> &r) { emit jobFinished(r); }
    void finishList(const QList<FoundDevice> &d, const QString &e = QString())
    {
        emit watchesListed(d, e);
    }
    QList<SessionJob> jobs;
    int lists;
};

class FakeSource : public NotifSource
{
public:
    FakeSource() : starts(0), stops(0), st(0) {}
    void start() { ++starts; if (st == 0) st = 1; }
    void stop() { ++stops; st = 0; }
    int state() const { return st; }
    QString reason() const { return why; }
    void setDebugLogging(bool) {}
    void push(const QString &app, const QString &summary, const QString &body)
    {
        notiffilter::IncomingNotification n;
        n.appName = app;
        n.summary = summary;
        n.body = body;
        emit notificationReceived(n);
    }
    int starts, stops, st;
    QString why;
};

class FakePublisher : public StatusPublisher
{
public:
    void publish(const QString &summary, const QString &body)
    {
        summaries << summary;
        bodies << body;
    }
    QStringList summaries, bodies;
};

settings::WatchEntry watch(const QString &key, const QString &model, const QString &name, bool enabled,
                           bool risk = false)
{
    settings::WatchEntry w;
    w.key = key;
    w.address = key;
    w.deviceName = name;
    w.modelId = model;
    w.alias = QStringLiteral("alias-") + key;
    w.enabled = enabled;
    w.riskAcknowledged = risk;
    return w;
}

watchlink::SessionResult okResult(const QString &key, bool timeSent, int alerts,
                                  const QDateTime &when)
{
    watchlink::SessionResult r;
    r.key = key;
    r.connected = true;
    r.timeSent = timeSent;
    r.alertsSent = alerts;
    r.when = when;
    return r;
}

} // namespace

class TstService : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    settings::Settings *m_settings;
    settings::State *m_state;
    watchproto::ModelTable m_models;
    FakeClock m_clock;
    FakeWake m_wake;
    FakeRunner m_runner;
    FakeSource m_source;
    FakePublisher m_pub;
    ServiceCore *m_core;
    int m_n;

    void setUpCore()
    {
        delete m_core;
        delete m_settings;
        delete m_state;
        ++m_n;
        const QString base = m_dir.path() + "/" + QString::number(m_n);
        QDir().mkpath(base);
        m_settings = new settings::Settings(base + "/s.conf");
        m_state = new settings::State(base + "/state.json");
        m_clock.t = QDateTime(QDate(2026, 10, 26), QTime(8, 0, 0), Qt::UTC);
        m_wake.scheduled = false;
        m_wake.schedules = m_wake.cancels = 0;
        m_runner.jobs.clear();
        m_runner.lists = 0;
        m_source.starts = m_source.stops = m_source.st = 0;
        m_pub.summaries.clear();
        m_pub.bodies.clear();
        m_wake.count = m_wake.maxCount = m_wake.acquires = 0;
        ServiceDeps d;
        d.settings = m_settings;
        d.state = m_state;
        d.models = &m_models;
        d.clock = &m_clock;
        d.wake = &m_wake;
        d.runner = &m_runner;
        d.monitor = &m_source;
        d.publisher = &m_pub;
        m_core = new ServiceCore(d);
    }

    void twoWatches()
    {
        QList<settings::WatchEntry> l;
        l << watch("AA:01", "dw-h5600", "CASIO DW-H5600", true)
          << watch("AA:02", "dw-h5600", "CASIO DW-H5600", true);
        QVERIFY(m_settings->setWatches(l));
    }

    // Finishes the job in flight with a success for every requested session.
    void finishAllOk()
    {
        QVERIFY(!m_runner.jobs.isEmpty());
        const SessionJob job = m_runner.jobs.last();
        QList<watchlink::SessionResult> rs;
        for (int i = 0; i < job.requests.size(); ++i)
            rs << okResult(job.requests.at(i).watch.key, job.requests.at(i).timeSync,
                           job.alerts.size(), m_clock.t);
        m_runner.finish(rs);
    }

public:
    TstService() : m_settings(0), m_state(0), m_core(0), m_n(0) {}

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QString err;
        QVERIFY2(m_models.load(QByteArray(kModels), &err), qPrintable(err));
    }

    void init() { setUpCore(); }
    void cleanup() {}

    // ------------------------------------------------------------ first run

    void firstRunWithoutFilesWorks()
    {
        m_core->start();
        QCOMPARE(m_runner.jobs.size(), 0);
        QVERIFY(m_core->status().value("watches").isValid());
        QCOMPARE(m_pub.bodies.last(), QString("No watch enabled"));
        // Nothing to wait for, master off: no wake-up asked.
        QVERIFY(!m_wake.scheduled);
        QCOMPARE(m_source.starts, 0);
    }

    void corruptStateFileDoesNotStopTheService()
    {
        QFile f(m_state->path());
        QDir().mkpath(QFileInfo(f).absolutePath());
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("{ this is not json");
        f.close();
        twoWatches();
        m_core->start();
        QCOMPARE(m_runner.jobs.size(), 1);    // never synced: both due
    }

    // ------------------------------------------------------------ time sync

    void dueWatchesGetOneSessionEachInListOrder()
    {
        twoWatches();
        m_core->start();
        QCOMPARE(m_runner.jobs.size(), 1);
        const SessionJob job = m_runner.jobs.at(0);
        QCOMPARE(job.requests.size(), 2);
        QCOMPARE(job.requests.at(0).watch.key, QString("AA:01"));
        QCOMPARE(job.requests.at(1).watch.key, QString("AA:02"));
        QVERIFY(job.requests.at(0).timeSync && job.requests.at(1).timeSync);
        QVERIFY(job.alerts.isEmpty());
        QVERIFY(m_core->busy());
        QCOMPARE(m_wake.count, 1);
        QCOMPARE(m_core->status().value("busy").toBool(), true);
    }

    void successIsStoredAndNextWakeIsAnInterval()
    {
        twoWatches();
        m_core->start();
        watchlink::SessionResult r1 = okResult("AA:01", true, 0, m_clock.t);
        r1.bleAddress = "11:22:33:44:55:66";
        m_runner.finish(QList<watchlink::SessionResult>() << r1 << okResult("AA:02", true, 0, m_clock.t));
        const settings::WatchState ws = m_state->watch("AA:01");
        QCOMPARE(ws.lastSuccess, m_clock.t);
        QCOMPARE(ws.lastAttempt, m_clock.t);
        QCOMPARE(ws.consecutiveFailures, 0);
        QCOMPARE(ws.bleAddress, QString("11:22:33:44:55:66"));
        QCOMPARE(ws.lastOpKind, QString("timesync"));
        QVERIFY(m_wake.scheduled);
        // default interval 24 h
        QCOMPARE(m_wake.when, m_clock.t.addSecs(24 * 3600));
        QCOMPARE(m_wake.count, 0);
        // survives a restart: state.json was written
        settings::State again(m_state->path());
        QVERIFY(again.load());
        QCOMPARE(again.watch("AA:01").lastSuccess, m_clock.t);
        QCOMPARE(m_pub.bodies.last(), QString("Last sync 08:00"));
    }

    void failureFeedsTheBackoff()
    {
        twoWatches();
        m_core->start();
        watchlink::SessionResult f;
        f.key = "AA:01";
        f.error = "Connect timed out";
        f.when = m_clock.t;
        m_runner.finish(QList<watchlink::SessionResult>() << f << okResult("AA:02", true, 0, m_clock.t));
        const settings::WatchState ws = m_state->watch("AA:01");
        QCOMPARE(ws.consecutiveFailures, 1);
        QCOMPARE(ws.lastError, QString("Connect timed out"));
        QVERIFY(!ws.lastSuccess.isValid());
        // First failure: retry in 5 minutes.
        QCOMPARE(m_wake.when, m_clock.t.addSecs(5 * 60));
        QCOMPARE(m_runner.jobs.size(), 1);     // not asked again at once

        m_clock.t = m_clock.t.addSecs(5 * 60);
        m_wake.fire();
        QCOMPARE(m_runner.jobs.size(), 2);
        QCOMPARE(m_runner.jobs.at(1).requests.size(), 1);
        QCOMPARE(m_runner.jobs.at(1).requests.at(0).watch.key, QString("AA:01"));
        QCOMPARE(m_runner.jobs.at(1).requests.at(0).expectedBleAddress, QString());
    }

    void missingResultCountsAsFailure()
    {
        QList<settings::WatchEntry> l;
        l << watch("AA:01", "dw-h5600", "CASIO DW-H5600", true);
        m_settings->setWatches(l);
        m_core->start();
        m_runner.finish(QList<watchlink::SessionResult>());
        QCOMPARE(m_state->watch("AA:01").consecutiveFailures, 1);
        QCOMPARE(m_runner.jobs.size(), 1);     // and no loop
    }

    void secondWakeDoesNotRepeatASync()
    {
        twoWatches();
        m_core->start();
        finishAllOk();
        m_clock.t = m_clock.t.addSecs(3600);
        m_wake.fire();
        QCOMPARE(m_runner.jobs.size(), 1);
        m_clock.t = m_clock.t.addSecs(23 * 3600);
        m_wake.fire();
        QCOMPARE(m_runner.jobs.size(), 2);
    }

    void wakeWhileJobRunsDoesNotStartASecondJob()
    {
        twoWatches();
        m_core->start();
        m_wake.fire();
        QCOMPARE(m_runner.jobs.size(), 1);
        QCOMPARE(m_wake.count, 1);
    }

    void awakeHoldCoversTheWholeJob()
    {
        twoWatches();
        m_core->start();
        QCOMPARE(m_wake.count, 1);
        finishAllOk();
        QCOMPARE(m_wake.count, 0);
        QCOMPARE(m_wake.maxCount, 1);
        // the wake-up is requested only after the job: never while a session runs
        QVERIFY(m_wake.scheduled);
    }

    void syncNowOneWatchAndAll()
    {
        twoWatches();
        m_core->start();
        finishAllOk();
        QCOMPARE(m_runner.jobs.size(), 1);
        QVERIFY(m_core->syncNow("AA:02"));
        QCOMPARE(m_runner.jobs.size(), 2);
        QCOMPARE(m_runner.jobs.at(1).requests.size(), 1);
        QCOMPARE(m_runner.jobs.at(1).requests.at(0).watch.key, QString("AA:02"));
        QCOMPARE(m_core->status().value("watches").value<QList<QVariantMap> >().at(1)
                     .value("running").toBool(), true);
        finishAllOk();
        QVERIFY(m_core->syncNow(QString()));
        QCOMPARE(m_runner.jobs.at(2).requests.size(), 2);
        finishAllOk();
        QVERIFY(!m_core->syncNow("nope"));
    }

    void syncNowWhileBusyWaits()
    {
        twoWatches();
        m_core->start();
        QVERIFY(m_core->syncNow("AA:01"));
        QCOMPARE(m_runner.jobs.size(), 1);     // the job in flight; one at a time
        finishAllOk();
        QCOMPARE(m_runner.jobs.size(), 2);     // then the requested one
    }

    void disabledWatchIsNotSynced()
    {
        QList<settings::WatchEntry> l;
        l << watch("AA:01", "dw-h5600", "CASIO DW-H5600", false)
          << watch("AA:02", "dw-h5600", "CASIO DW-H5600", true);
        m_settings->setWatches(l);
        m_core->start();
        QCOMPARE(m_runner.jobs.at(0).requests.size(), 1);
        QCOMPARE(m_runner.jobs.at(0).requests.at(0).watch.key, QString("AA:02"));
        QVERIFY(!m_core->syncNow("AA:01"));
    }

    // ------------------------------------------------------- risk acknowledged

    void untestedModelWithoutAcknowledgementIsSkipped()
    {
        QList<settings::WatchEntry> l;
        l << watch("BB:01", "gw-b5600", "CASIO GW-B5600", true, false)
          << watch("BB:02", "gw-b5600", "CASIO GW-B5600", true, true)
          << watch("AA:01", "dw-h5600", "CASIO DW-H5600", true, false);
        m_settings->setWatches(l);
        m_settings->setMasterOn(true);
        m_core->start();
        QCOMPARE(m_runner.jobs.at(0).requests.size(), 2);
        QCOMPARE(m_runner.jobs.at(0).requests.at(0).watch.key, QString("BB:02"));
        QCOMPARE(m_runner.jobs.at(0).requests.at(1).watch.key, QString("AA:01"));
        QVERIFY(!m_core->syncNow("BB:01"));
        finishAllOk();

        // alerts do not go there either
        m_settings->discoverApp("Messages");
        m_settings->setAppEnabled("Messages", true);
        m_source.push("Messages", "Alex", "Hello");
        const SessionJob job = m_runner.jobs.last();
        QCOMPARE(job.alertTargets.size(), 2);
        for (int i = 0; i < job.alertTargets.size(); ++i)
            QVERIFY(job.alertTargets.at(i).key != "BB:01");

        const QVariantMap st = m_core->status();
        QCOMPARE(st.value("riskProblems").toStringList(), QStringList() << "BB:01");
        const QList<QVariantMap> ws = st.value("watches").value<QList<QVariantMap> >();
        QCOMPARE(ws.at(0).value("usable").toBool(), false);
        QCOMPARE(ws.at(0).value("supportStatus").toString(), QString("untested"));
        QVERIFY(!ws.at(0).value("problem").toString().isEmpty());
        QCOMPARE(ws.at(1).value("usable").toBool(), true);
    }

    void unknownModelIsNotUsed()
    {
        QList<settings::WatchEntry> l;
        l << watch("CC:01", "nothing", "SOMETHING ELSE", true, true);
        m_settings->setWatches(l);
        m_core->start();
        QCOMPARE(m_runner.jobs.size(), 0);
    }

    // ----------------------------------------------------------- notifications

    void prepareForwarding()
    {
        twoWatches();
        m_settings->setMasterOn(true);
        m_settings->discoverApp("Messages");
        m_settings->setAppEnabled("Messages", true);
        m_core->start();
        finishAllOk();       // the first sync
    }

    void filteredNotificationReachesEveryEnabledWatch()
    {
        QList<settings::WatchEntry> l;
        l << watch("AA:01", "dw-h5600", "CASIO DW-H5600", true)
          << watch("AA:02", "dw-h5600", "CASIO DW-H5600", false)
          << watch("AA:03", "dw-h5600", "CASIO DW-H5600", true);
        m_settings->setWatches(l);
        m_settings->setMasterOn(true);
        m_settings->discoverApp("Messages");
        m_settings->setAppEnabled("Messages", true);
        m_core->start();
        finishAllOk();
        const int before = m_runner.jobs.size();
        m_source.push("Messages", "Alex", "Hello there");
        QCOMPARE(m_runner.jobs.size(), before + 1);
        const SessionJob job = m_runner.jobs.last();
        QCOMPARE(job.alerts.size(), 1);
        QCOMPARE(job.alertTargets.size(), 2);
        QCOMPARE(job.alertTargets.at(0).key, QString("AA:01"));
        QCOMPARE(job.alertTargets.at(1).key, QString("AA:03"));
        QCOMPARE(job.requests.size(), 2);
        QVERIFY(!job.requests.at(0).timeSync);
        QVERIFY(job.alerts.at(0).alert);
        QCOMPARE(QString::fromStdString(job.alerts.at(0).title), QString("Alex"));
        QCOMPARE(QString::fromStdString(job.alerts.at(0).message), QString("Hello there"));
        QCOMPARE(QString::fromStdString(job.alerts.at(0).sender), QString("Messages"));
        QCOMPARE(job.alerts.at(0).category, uint8_t(0));     // no app name shipped
        QCOMPARE(m_wake.count, 1);
        finishAllOk();
        QCOMPARE(m_state->watch("AA:01").lastOpKind, QString("notif"));
        QCOMPARE(m_wake.count, 0);
    }

    void dropsDoNotReachTheQueue()
    {
        prepareForwarding();
        const int n = m_runner.jobs.size();
        m_source.push("Casio Watch Sync", "x", "y");        // own name
        m_source.push("Mail", "x", "y");                    // discovered, not enabled
        m_source.push("Messages", "", "");                  // empty
        QCOMPARE(m_runner.jobs.size(), n);
        QVERIFY(m_settings->apps().contains("Mail"));       // discoverApp reached Settings
        QVERIFY(!m_settings->isAppEnabled("Mail"));
        QVERIFY(!m_settings->apps().contains("Casio Watch Sync"));
    }

    void blacklistedAppIsNeitherForwardedNorListed()
    {
        prepareForwarding();
        m_settings->setBlacklist(QStringList() << "bank");
        const int n = m_runner.jobs.size();
        m_source.push("MyBank", "x", "y");
        QCOMPARE(m_runner.jobs.size(), n);
        QVERIFY(!m_settings->apps().contains("MyBank"));
    }

    void notificationsDuringAJobWaitAndGoTogether()
    {
        prepareForwarding();
        QVERIFY(m_core->syncNow(QString()));
        const int n = m_runner.jobs.size();
        m_source.push("Messages", "a", "1");
        m_source.push("Messages", "b", "2");
        QCOMPARE(m_runner.jobs.size(), n);
        finishAllOk();
        QCOMPARE(m_runner.jobs.size(), n + 1);
        QCOMPARE(m_runner.jobs.last().alerts.size(), 2);
        QCOMPARE(QString::fromStdString(m_runner.jobs.last().alerts.at(0).title), QString("a"));
    }

    void oldPendingNotificationIsDropped()
    {
        prepareForwarding();
        QVERIFY(m_core->syncNow(QString()));
        const int n = m_runner.jobs.size();
        m_source.push("Messages", "old", "1");
        m_clock.t = m_clock.t.addSecs(watchlink::kMaxAgeSeconds + 1);
        finishAllOk();
        QCOMPARE(m_runner.jobs.size(), n);      // nothing left to send
    }

    void testNotificationGoesToEveryUsableWatch()
    {
        twoWatches();
        m_core->start();
        finishAllOk();
        QVERIFY(m_core->testNotification());      // master off: still sent
        const SessionJob job = m_runner.jobs.last();
        QCOMPARE(job.alerts.size(), 1);
        QCOMPARE(job.alertTargets.size(), 2);
        QVERIFY(job.alerts.at(0).alert);
        QVERIFY(!job.alerts.at(0).message.empty());
        finishAllOk();
        m_settings->setWatches(QList<settings::WatchEntry>());
        QVERIFY(!m_core->testNotification());
    }

    void retryWakeAtNextAttempt()
    {
        prepareForwarding();
        m_source.push("Messages", "a", "1");
        const QDateTime retry = m_clock.t.addSecs(10);
        QList<watchlink::SessionResult> rs;
        for (int i = 0; i < 2; ++i) {
            watchlink::SessionResult r;
            r.key = i == 0 ? "AA:01" : "AA:02";
            r.error = "Connect timed out";
            r.alertsKept = 1;
            r.nextAttempt = retry;
            r.when = m_clock.t;
            rs << r;
        }
        m_runner.finish(rs);
        QVERIFY(m_wake.scheduled);
        QCOMPARE(m_wake.when, retry);
        QCOMPARE(m_wake.cancels, 0);       // schedule() replaces a pending request by itself
        // alerts-only failures do not touch the time-sync back-off
        QCOMPARE(m_state->watch("AA:01").consecutiveFailures, 0);
        QCOMPARE(m_state->watch("AA:01").lastError, QString("Connect timed out"));

        const int n = m_runner.jobs.size();
        m_clock.t = retry;
        m_wake.fire();
        QCOMPARE(m_runner.jobs.size(), n + 1);
        const SessionJob job = m_runner.jobs.last();
        QCOMPARE(job.requests.size(), 2);
        QVERIFY(!job.requests.at(0).timeSync);
        QVERIFY(job.alerts.isEmpty());
        finishAllOk();                   // nothing kept any more
        QVERIFY(m_wake.when > retry.addSecs(3600));
    }

    // Found on the phone: a second alert queued while the first retry was
    // pending gave a "skipped" result, which cleared the retry wake-up.
    void skippedSessionKeepsTheRetryWake()
    {
        prepareForwarding();
        m_source.push("Messages", "a", "1");
        const QDateTime retry = m_clock.t.addSecs(10);
        QList<watchlink::SessionResult> rs;
        for (int i = 0; i < 2; ++i) {
            watchlink::SessionResult r;
            r.key = i == 0 ? "AA:01" : "AA:02";
            r.skipped = true;
            r.alertsKept = 2;
            r.nextAttempt = retry;
            r.when = m_clock.t;
            rs << r;
        }
        m_runner.finish(rs);
        QVERIFY(m_wake.scheduled);
        QCOMPARE(m_wake.when, retry);
    }

    void monitorIsRestartedAtEveryWake()
    {
        m_settings->setMasterOn(true);
        m_core->start();
        QCOMPARE(m_source.starts, 1);
        m_source.st = 0;       // the connection was lost
        m_wake.fire();
        QCOMPARE(m_source.starts, 2);
        // with nothing else to wait for the wake-up still comes back (monitor)
        QVERIFY(m_wake.scheduled);
    }

    // ---------------------------------------------------------------- settings

    void masterOffStopsForwardingAtOnce()
    {
        prepareForwarding();
        QVERIFY(m_core->syncNow(QString()));
        m_source.push("Messages", "a", "1");       // waits behind the running job
        m_settings->setMasterOn(false);            // the UI wrote the file; ReloadSettings follows
        m_core->reloadSettings();
        QVERIFY(m_source.stops >= 1);
        QCOMPARE(m_source.st, 0);
        const int n = m_runner.jobs.size();
        finishAllOk();
        // the waiting alert is gone; the queues of the worker are emptied
        QCOMPARE(m_runner.jobs.size(), n + 1);
        QVERIFY(m_runner.jobs.last().alerts.isEmpty());
        QCOMPARE(m_runner.jobs.last().clearKeys.size(), 2);
        QCOMPARE(m_core->status().value("monitorState").toString(), QString("off"));
        // and a notification that still arrives is dropped by the filter
        m_source.push("Messages", "a", "1");
        QCOMPARE(m_runner.jobs.size(), n + 1);
    }

    void reloadSettingsPicksUpTheFile()
    {
        m_core->start();
        QCOMPARE(m_runner.jobs.size(), 0);
        // another process (the UI) writes the same file
        settings::Settings ui(m_settings->path());
        QList<settings::WatchEntry> l;
        l << watch("AA:01", "dw-h5600", "CASIO DW-H5600", true);
        ui.setWatches(l);
        ui.setMasterOn(true);
        m_core->reloadSettings();
        QCOMPARE(m_core->status().value("watches").value<QList<QVariantMap> >().size(), 1);
        QCOMPARE(m_runner.jobs.size(), 1);     // the new watch is synced at once
        QCOMPARE(m_source.starts, 1);
    }

    void intervalChangeMovesTheWakeUp()
    {
        QList<settings::WatchEntry> l;
        l << watch("AA:01", "dw-h5600", "CASIO DW-H5600", true);
        m_settings->setWatches(l);
        m_core->start();
        finishAllOk();
        QCOMPARE(m_wake.when, m_clock.t.addSecs(24 * 3600));
        m_settings->setIntervalMinutes(6 * 60);
        QCOMPARE(m_wake.when, m_clock.t.addSecs(6 * 3600));
    }

    // ------------------------------------------------------------------ status

    void getStatusContent()
    {
        QList<settings::WatchEntry> l;
        l << watch("AA:01", "dw-h5600", "CASIO DW-H5600", true);
        m_settings->setWatches(l);
        m_settings->setMasterOn(true);
        m_core->start();
        watchlink::SessionResult r = okResult("AA:01", true, 0, m_clock.t);
        m_runner.finish(QList<watchlink::SessionResult>() << r);
        const QVariantMap st = m_core->status();
        QCOMPARE(st.value("serviceState").toString(), QString("running"));
        QCOMPARE(st.value("masterOn").toBool(), true);
        QCOMPARE(st.value("monitorState").toString(), QString("running"));
        QCOMPARE(st.value("busy").toBool(), false);
        const QList<QVariantMap> ws = st.value("watches").value<QList<QVariantMap> >();
        QCOMPARE(ws.size(), 1);
        const QVariantMap w = ws.at(0);
        QCOMPARE(w.value("key").toString(), QString("AA:01"));
        QCOMPARE(w.value("alias").toString(), QString("alias-AA:01"));
        QCOMPARE(w.value("modelLabel").toString(), QString("Casio DW-H5600"));
        QCOMPARE(w.value("supportStatus").toString(), QString("supported"));
        QVERIFY(w.contains("silhouette"));
        QCOMPARE(w.value("silhouette").toString(), QString());
        QCOMPARE(w.value("defaultSilhouette").toString(), QString("dw-h5600"));
        QCOMPARE(w.value("enabled").toBool(), true);
        QCOMPARE(w.value("running").toBool(), false);
        QCOMPARE(w.value("lastSuccess").toLongLong(), m_clock.t.toMSecsSinceEpoch() / 1000);
        QCOMPARE(w.value("nextSync").toLongLong(), m_clock.t.addSecs(24 * 3600).toMSecsSinceEpoch() / 1000);
        QCOMPARE(w.value("lastError").toString(), QString());
        QCOMPARE(w.value("lastOpKind").toString(), QString("timesync"));
        // the BLE address is for the app's use only
        QVERIFY(!w.contains("bleAddress"));
    }

    void getStatusSilhouetteKeys()
    {
        QList<settings::WatchEntry> l;
        settings::WatchEntry a = watch("AA:01", "dw-h5600", "CASIO DW-H5600", true);
        a.silhouette = "round";
        settings::WatchEntry b = watch("AA:02", "gw-b5600", "CASIO GW-B5600", true);  // model without default
        settings::WatchEntry c = watch("AA:03", "no-such-model", "Some Watch", true); // unknown model
        c.silhouette = "whatever";
        l << a << b << c;
        QVERIFY(m_settings->setWatches(l));
        const QList<QVariantMap> ws = m_core->status().value("watches").value<QList<QVariantMap> >();
        QCOMPARE(ws.size(), 3);
        for (int i = 0; i < 3; ++i) {
            QVERIFY(ws.at(i).contains("silhouette"));
            QVERIFY(ws.at(i).contains("defaultSilhouette"));
            QCOMPARE(ws.at(i).value("silhouette").type(), QVariant::String);
            QCOMPARE(ws.at(i).value("defaultSilhouette").type(), QVariant::String);
        }
        QCOMPARE(ws.at(0).value("silhouette").toString(), QString("round"));
        QCOMPARE(ws.at(0).value("defaultSilhouette").toString(), QString("dw-h5600"));
        QCOMPARE(ws.at(1).value("silhouette").toString(), QString());
        QCOMPARE(ws.at(1).value("defaultSilhouette").toString(), QString());
        QCOMPARE(ws.at(2).value("silhouette").toString(), QString("whatever"));
        QCOMPARE(ws.at(2).value("defaultSilhouette").toString(), QString());
    }

    void monitorProblemIsReported()
    {
        m_settings->setMasterOn(true);
        m_source.why = "denied";
        m_core->start();
        m_source.st = 2;
        QCOMPARE(m_core->status().value("monitorState").toString(), QString("cannot-monitor"));
        QCOMPARE(m_core->status().value("monitorReason").toString(), QString("denied"));
    }

    void statusChangedIsEmitted()
    {
        twoWatches();
        QSignalSpy spy(m_core, SIGNAL(statusChanged()));
        m_core->start();
        QVERIFY(spy.count() >= 1);
        spy.clear();
        finishAllOk();
        QVERIFY(spy.count() >= 1);
    }

    void listWatchesMergesSettingsAndBluez()
    {
        QList<settings::WatchEntry> l;
        l << watch("AA:01", "dw-h5600", "CASIO DW-H5600", true)
          << watch("AA:09", "no-such-model", "Some Watch", false);   // unknown model
        l[0].silhouette = "round";
        m_settings->setWatches(l);
        m_core->start();
        finishAllOk();
        QSignalSpy spy(m_core, SIGNAL(watchListReady(quint32, QList<QVariantMap>, QString)));
        const quint32 t = m_core->requestWatchList();
        QTRY_COMPARE(m_runner.lists, 1);
        FoundDevice a, b;
        a.address = "AA:01"; a.name = "CASIO DW-H5600"; a.alias = "My watch";
        a.modelId = "dw-h5600"; a.modelLabel = "Casio DW-H5600"; a.supported = true;
        b.address = "BB:07"; b.name = "CASIO GW-B5600"; b.alias = "Other";
        b.modelId = "gw-b5600"; b.modelLabel = "Casio GW-B5600"; b.supported = false;
        m_runner.finishList(QList<FoundDevice>() << a << b);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toUInt(), t);
        const QList<QVariantMap> out = qvariant_cast<QList<QVariantMap> >(spy.at(0).at(1));
        QCOMPARE(out.size(), 3);
        QCOMPARE(out.at(0).value("key").toString(), QString("AA:01"));
        QCOMPARE(out.at(0).value("foundNow").toBool(), true);
        QCOMPARE(out.at(0).value("inSettings").toBool(), true);
        QCOMPARE(out.at(1).value("supportStatus").toString(), QString("untested"));
        QCOMPARE(out.at(1).value("inSettings").toBool(), false);
        QCOMPARE(out.at(2).value("key").toString(), QString("AA:09"));
        QCOMPARE(out.at(2).value("foundNow").toBool(), false);
        // silhouette keys are always present, as strings
        for (int i = 0; i < 3; ++i) {
            QVERIFY(out.at(i).contains("silhouette"));
            QVERIFY(out.at(i).contains("defaultSilhouette"));
        }
        QCOMPARE(out.at(0).value("silhouette").toString(), QString("round"));
        QCOMPARE(out.at(0).value("defaultSilhouette").toString(), QString("dw-h5600"));
        QCOMPARE(out.at(1).value("silhouette").toString(), QString());          // not in settings
        QCOMPARE(out.at(1).value("defaultSilhouette").toString(), QString());   // model has none
        QCOMPARE(out.at(2).value("silhouette").toString(), QString());
        QCOMPARE(out.at(2).value("defaultSilhouette").toString(), QString());   // unknown model
        QCOMPARE(m_wake.count, 0);
    }

    void listWatchesWaitsForARunningJob()
    {
        twoWatches();
        m_core->start();
        m_core->requestWatchList();
        QTest::qWait(20);
        QCOMPARE(m_runner.lists, 0);       // the worker is busy
        finishAllOk();
        QCOMPARE(m_runner.lists, 1);
        QCOMPARE(m_wake.count, 1);
        m_runner.finishList(QList<FoundDevice>());
        QCOMPARE(m_wake.count, 0);
    }

    // ------------------------------------------------------------------ resident

    void residentNotificationTextAndOwnName()
    {
        twoWatches();
        m_core->start();
        QCOMPARE(m_pub.summaries.last(), notifmonitor::ownAppName());
        QCOMPARE(m_pub.bodies.last(), QString("Not synced yet"));
        // two watches: the oldest last success counts
        watchlink::SessionResult a = okResult("AA:01", true, 0, m_clock.t);
        watchlink::SessionResult b = okResult("AA:02", true, 0, m_clock.t.addSecs(600));
        m_runner.finish(QList<watchlink::SessionResult>() << a << b);
        QCOMPARE(m_pub.bodies.last(), QString("Last sync 08:00"));
        // publishing again with the same text is skipped
        const int n = m_pub.bodies.size();
        m_core->reloadSettings();
        QCOMPARE(m_pub.bodies.size(), n);
    }

    // The resident notification must not come back through the monitor.
    void ownNotificationIsDropped()
    {
        prepareForwarding();
        const int n = m_runner.jobs.size();
        m_source.push(notifmonitor::ownAppName(), "Casio Watch Sync", "Last sync 08:00");
        QCOMPARE(m_runner.jobs.size(), n);
    }

    // ------------------------------------------------------------------- D-Bus

    void dbusApiAndSecondInstance()
    {
        QProcess daemon;
        const QString conf = m_dir.path() + "/bus.conf";
        const QString sock = m_dir.path() + "/bus";
        {
            QFile f(conf);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QTextStream t(&f);
            t << "<busconfig><type>session</type><listen>unix:path=" << sock
              << "</listen><auth>EXTERNAL</auth><policy context=\"default\">"
                 "<allow send_destination=\"*\" eavesdrop=\"true\"/><allow eavesdrop=\"true\"/>"
                 "<allow own=\"*\"/></policy></busconfig>\n";
        }
        daemon.start("dbus-daemon", QStringList() << "--config-file" << conf << "--nofork");
        if (!daemon.waitForStarted(3000))
            QSKIP("dbus-daemon cannot be started here");
        for (int i = 0; i < 100 && !QFile::exists(sock); ++i)
            QTest::qWait(50);
        if (!QFile::exists(sock))
            QSKIP("dbus-daemon cannot be started here");
        const QString addr = "unix:path=" + sock;

        twoWatches();
        QDBusConnection server = QDBusConnection::connectToBus(addr, "tst-service-server");
        QDBusConnection client = QDBusConnection::connectToBus(addr, "tst-service-client");
        ServiceObject holder;
        QVERIFY(registerOnBus(server, m_core, &holder));
        m_core->start();

        // a second instance cannot take the name
        QDBusConnection other = QDBusConnection::connectToBus(addr, "tst-service-other");
        ServiceObject holder2;
        QVERIFY(!registerOnBus(other, m_core, &holder2));

        qDBusRegisterMetaType<QList<QVariantMap> >();
        QDBusMessage m = QDBusMessage::createMethodCall(kBusName, kObjectPath, kInterfaceName, "GetStatus");
        QDBusPendingCall pc = client.asyncCall(m, 5000);
        QDBusPendingCallWatcher w(pc);
        QSignalSpy done(&w, SIGNAL(finished(QDBusPendingCallWatcher*)));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        QDBusPendingReply<QVariantMap> reply = pc;
        QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
        const QVariantMap st = reply.value();
        QCOMPARE(st.value("serviceState").toString(), QString("running"));
        const QDBusArgument arg = st.value("watches").value<QDBusArgument>();
        const QList<QVariantMap> ws = qdbus_cast<QList<QVariantMap> >(arg);
        QCOMPARE(ws.size(), 2);
        QCOMPARE(ws.at(0).value("key").toString(), QString("AA:01"));
        QVERIFY(ws.at(0).value("lastSuccess").toLongLong() == 0);

        // SyncNow with a key that does not exist
        QDBusMessage s = QDBusMessage::createMethodCall(kBusName, kObjectPath, kInterfaceName, "SyncNow");
        s << QString("nope");
        QDBusPendingCall pc2 = client.asyncCall(s, 5000);
        QDBusPendingCallWatcher w2(pc2);
        QSignalSpy done2(&w2, SIGNAL(finished(QDBusPendingCallWatcher*)));
        QTRY_COMPARE_WITH_TIMEOUT(done2.count(), 1, 5000);
        QDBusPendingReply<bool> r2 = pc2;
        QVERIFY(r2.isValid());
        QCOMPARE(r2.value(), false);

        // ListWatches is answered later, when the worker has looked
        finishAllOk();            // the first sync of start()
        QDBusMessage lw = QDBusMessage::createMethodCall(kBusName, kObjectPath, kInterfaceName, "ListWatches");
        QDBusPendingCall pc3 = client.asyncCall(lw, 5000);
        QDBusPendingCallWatcher w3(pc3);
        QSignalSpy done3(&w3, SIGNAL(finished(QDBusPendingCallWatcher*)));
        QTRY_COMPARE_WITH_TIMEOUT(m_runner.lists, 1, 5000);
        FoundDevice d;
        d.address = "AA:01"; d.name = "CASIO DW-H5600"; d.modelId = "dw-h5600";
        d.modelLabel = "Casio DW-H5600"; d.supported = true;
        m_runner.finishList(QList<FoundDevice>() << d);
        QTRY_COMPARE_WITH_TIMEOUT(done3.count(), 1, 5000);
        QDBusPendingReply<QList<QVariantMap> > r3 = pc3;
        QVERIFY2(r3.isValid(), qPrintable(r3.error().message()));
        QCOMPARE(r3.value().size(), 2);        // AA:01 found, AA:02 only in the settings
        QCOMPARE(r3.value().at(0).value("foundNow").toBool(), true);

        QDBusConnection::disconnectFromBus("tst-service-client");
        QDBusConnection::disconnectFromBus("tst-service-other");
        QDBusConnection::disconnectFromBus("tst-service-server");
        daemon.terminate();
        daemon.waitForFinished(3000);
    }

    void cleanupTestCase()
    {
        delete m_core;
        m_core = 0;
    }
};

QTEST_GUILESS_MAIN(TstService)
#include "tst_service.moc"
