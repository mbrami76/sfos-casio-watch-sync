// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Tests for watchlink: session logic and queue with a fake BluezAdapter. The real adapter
// is not tested here.
#include "watchlink.h"

#include <QtTest>

using namespace watchlink;
using settings::WatchEntry;

namespace {

const char kModelsJson[] =
    "{\"version\":1,\"models\":["
    "{\"id\":\"dw-h5600\",\"label\":\"Casio DW-H5600\",\"family\":\"h5600\","
    "\"status\":\"supported\",\"namePatterns\":[\"CASIO DW-H5600\"],"
    "\"params\":{\"dowEncoding\":\"sun0\"}},"
    "{\"id\":\"mon-model\",\"label\":\"Mon model\",\"family\":\"x\","
    "\"status\":\"untested\",\"namePatterns\":[\"CASIO MON-1\"],"
    "\"params\":{\"dowEncoding\":\"mon1\"}}]}";

class FakeClock : public scheduler::ClockSource
{
public:
    FakeClock() : t(QDateTime(QDate(2026, 10, 25), QTime(12, 34, 56), Qt::LocalTime)) {}
    QDateTime now() const { return t; }
    QDateTime t;
};

// Records every call as a text line; failures are switched on by name.
class FakeAdapter : public BluezAdapter
{
public:
    FakeAdapter() : connectedNow(0), maxConnected(0), connectTimeoutSeen(0), hook(0) {}

    struct Write { QString path, uuid; QByteArray value; bool withResponse; };

    bool devices(QList<BluezDevice> *out, QString *error)
    {
        calls << "devices";
        if (failDevices) { *error = "no bluez"; return false; }
        *out = devs;
        return true;
    }
    bool connectDevice(const QString &path, int timeoutMs, QString *error)
    {
        calls << "connect " + path;
        connectTimeoutSeen = timeoutMs;
        if (failConnect.contains(path)) { *error = failConnect.value(path); return false; }
        ++connectedNow;
        maxConnected = qMax(maxConnected, connectedNow);
        return true;
    }
    bool waitServicesResolved(const QString &, int, QString *error)
    {
        calls << "resolved";
        if (failStep == "resolved") { *error = "timeout"; return false; }
        return true;
    }
    bool startNotify(const QString &, const QString &uuid, QString *error)
    {
        calls << "notify " + uuid.left(8);
        if (failStep == "notify") { *error = "not permitted"; return false; }
        return true;
    }
    bool write(const QString &path, const QString &uuid, const QByteArray &value,
               bool withResponse, QString *error)
    {
        Write w;
        w.path = path; w.uuid = uuid; w.value = value; w.withResponse = withResponse;
        writes << w;
        calls << QString("write %1 %2").arg(uuid.left(8), withResponse ? "req" : "cmd");
        if (hook) hook->onWrite(writes.size());
        if (failWriteAt == writes.size()) { *error = "write failed"; return false; }
        return true;
    }
    bool readFeature(const QString &, quint8 feature, int, QByteArray *answer, QString *error)
    {
        calls << QString("read %1").arg(feature, 2, 16, QLatin1Char('0'));
        if (!featureAnswerOk) { *error = "no answer"; return false; }
        *answer = featureAnswer;
        return true;
    }
    void disconnectDevice(const QString &path)
    {
        calls << "disconnect " + path;
        if (connectedNow > 0) --connectedNow;
    }
    void settle(int) { calls << "settle"; }

    struct Hook { virtual ~Hook() {} virtual void onWrite(int n) = 0; };

    QList<BluezDevice> devs;
    QStringList calls;
    QList<Write> writes;
    QMap<QString, QString> failConnect;
    QString failStep;
    int failWriteAt = 0;                // 1-based index of the write that fails
    bool failDevices = false;
    bool featureAnswerOk = true;
    QByteArray featureAnswer;
    int connectedNow, maxConnected, connectTimeoutSeen;
    Hook *hook;
};

BluezDevice dev(const QString &path, const QString &addr, const QString &name = "CASIO DW-H5600",
                bool paired = true)
{
    BluezDevice d;
    d.path = path;
    d.address = addr;
    d.name = name;
    d.alias = name;
    d.paired = paired;
    return d;
}

WatchEntry entry(const QString &addr, const QString &name = "CASIO DW-H5600", bool enabled = true)
{
    WatchEntry e;
    e.key = addr;
    e.address = addr;
    e.deviceName = name;
    e.modelId = "dw-h5600";
    e.alias = "w";
    e.enabled = enabled;
    return e;
}

SessionRequest req(const WatchEntry &e, bool sync = false)
{
    SessionRequest r;
    r.watch = e;
    r.timeSync = sync;
    return r;
}

notiffilter::WatchNotification alertOf(const char *app, const char *title, const char *msg,
                                       uint8_t cat = 4)
{
    notiffilter::WatchNotification n;
    n.alert = false;   // watchlink forces the flag on
    n.category = cat;
    n.sender = app;
    n.title = title;
    n.message = msg;
    return n;
}

watchproto::DecodedFrame decodeWrite(const FakeAdapter::Write &w)
{
    watchproto::DecodedFrame d;
    watchproto::Bytes b(w.value.begin(), w.value.end());
    bool ok = watchproto::decodeFrame(b, &d);
    Q_ASSERT(ok);
    Q_UNUSED(ok);
    return d;
}

QByteArray feature10(const char *bleBytesReversed)   // 6 bytes as stored on the wire
{
    QByteArray a;
    a.append(char(0x10));
    a.append(QByteArray::fromHex(bleBytesReversed));
    a.append(QByteArray::fromHex("0e030fffffffff3201 0400"));
    return a;
}

} // namespace

class TstWatchlink : public QObject, public FakeAdapter::Hook
{
    Q_OBJECT
public:
    void onWrite(int n) { Q_UNUSED(n); if (reentrant) reentrantResult = link->runSession(req(entry("A"), true)); }

private slots:
    void init();
    void cleanup();

    void syncOrder();
    void syncUsesModelDowEncoding();
    void forwardOrder();
    void syncAndAlertsTogether();
    void writesAreSpaced();
    void nothingQueuedDoesNotConnect();
    void queueLimitDropsOldest();
    void queueAgeing();
    void alertsGoToEveryEnabledWatch();
    void outOfRangeKeepsAlertsAndRetries();
    void retriedAlertsAgeOut();
    void connectedButNotDeliveredIsLost();
    void retryQueueIsCircularAndSyncIsNotHeldBack();
    void connectTimeoutPath();
    void bluezErrorsAreFailures();
    void alertWriteFailureStopsSession();
    void invalidClockIsRefused();
    void listOrderAndNeverConcurrent();
    void busyGuard();
    void deviceLookup();
    void identityCheck();
    void bleAddressParsing();

private:
    FakeAdapter *fa;
    FakeClock *clock;
    watchproto::ModelTable *models;
    WatchLink *link;
    bool reentrant;
    SessionResult reentrantResult;
};

void TstWatchlink::init()
{
    fa = new FakeAdapter;
    clock = new FakeClock;
    models = new watchproto::ModelTable;
    QString err;
    QVERIFY2(models->load(QByteArray(kModelsJson), &err), qPrintable(err));
    link = new WatchLink(fa, models, clock);
    link->setWriteGapMs(0);
    fa->devs << dev("/dev/A", "AA:AA:AA:AA:AA:01") ;
    fa->hook = this;
    reentrant = false;
    fa->featureAnswer = feature10("060504030201");
}

void TstWatchlink::cleanup()
{
    delete link; delete models; delete clock; delete fa;
}

void TstWatchlink::syncOrder()
{
    SessionResult r = link->runSession(req(entry("AA:AA:AA:AA:AA:01"), true));
    QVERIFY2(r.ok(), qPrintable(r.error));
    QVERIFY(r.connected && r.timeSent);
    QCOMPARE(r.alertsSent, 0);
    QCOMPARE(r.deviceAddress, QString("AA:AA:AA:AA:AA:01"));
    QCOMPARE(fa->calls, QStringList() << "devices" << "connect /dev/A" << "resolved"
                                      << "notify 26eb002d" << "write 26eb002d req"
                                      << "write 26eb0030 cmd" << "settle"
                                      << "disconnect /dev/A");
    QCOMPARE(fa->connectTimeoutSeen, kConnectTimeoutMs);

    // Time frame: the phone clock 2026-10-25 12:34:56 (Sunday, dow 0), same bytes as watchproto.
    watchproto::TimeFrame tf = watchproto::buildTimeFrame(watchproto::DateTime(2026, 10, 25, 12, 34, 56));
    QVERIFY(tf.ok());
    QCOMPARE(fa->writes[0].value, QByteArray(reinterpret_cast<const char *>(tf.bytes.data()),
                                             int(tf.bytes.size())));
    QCOMPARE(fa->writes[0].value.size(), 11);
    QCOMPARE(int(quint8(fa->writes[0].value[8])), 0);

    // Link status: silent, CWS / time sync, stamped with the phone time.
    watchproto::DecodedFrame st = decodeWrite(fa->writes[1]);
    QCOMPARE(QString::fromStdString(st.sender), QString("CWS"));
    QCOMPARE(QString::fromStdString(st.title), QString("Sync"));
    QCOMPARE(int(st.alert), 0);
    QCOMPARE(int(st.deleteFlag), 0);
    QCOMPARE(int(st.category), 0);
    QCOMPARE(st.timestamp.hour, 12);
    QCOMPARE(st.timestamp.minute, 34);
    QCOMPARE(st.timestamp.second, 56);
    QVERIFY(QString::fromStdString(st.message).startsWith("Casio Watch Sync\nLast contact with phone"));
}

void TstWatchlink::syncUsesModelDowEncoding()
{
    // Sunday 2026-10-25: 0 in "sun0", 7 in "mon1" (R-TIME-7: a per-model parameter).
    fa->devs << dev("/dev/M", "AA:AA:AA:AA:AA:09", "CASIO MON-1");
    QVERIFY(link->runSession(req(entry("AA:AA:AA:AA:AA:09", "CASIO MON-1"), true)).ok());
    QCOMPARE(int(quint8(fa->writes[0].value[8])), 7);
}

void TstWatchlink::forwardOrder()
{
    WatchEntry e = entry("AA:AA:AA:AA:AA:01");
    link->enqueueAlert(alertOf("Messages", "Alex", "Hello"), QList<WatchEntry>() << e);
    link->enqueueAlert(alertOf("Mail", "Subject", "Body\r\nline", 6), QList<WatchEntry>() << e);
    QCOMPARE(link->queued(e.key), 2);
    clock->t = clock->t.addSecs(30);

    SessionResult r = link->runSession(req(e));
    QVERIFY2(r.ok(), qPrintable(r.error));
    QVERIFY(r.connected);
    QVERIFY(!r.timeSent);
    QCOMPARE(r.alertsSent, 2);
    QCOMPARE(r.alertsDropped, 0);
    QCOMPARE(link->queued(e.key), 0);
    QCOMPARE(fa->calls, QStringList() << "devices" << "connect /dev/A" << "resolved"
                                      << "notify 26eb002d" << "write 26eb0030 cmd"
                                      << "write 26eb0030 cmd" << "write 26eb0030 cmd"
                                      << "settle" << "disconnect /dev/A");
    QCOMPARE(fa->writes.size(), 3);
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(fa->writes[i].uuid.left(8), QString("26eb0030"));
        QVERIFY(!fa->writes[i].withResponse);
    }
    // Status first (silent), naming the app of the newest alert; then the alerts, alert flag set.
    watchproto::DecodedFrame st = decodeWrite(fa->writes[0]);
    QCOMPARE(QString::fromStdString(st.sender), QString("CWS"));
    QCOMPARE(QString::fromStdString(st.title), QString("Sync"));
    QCOMPARE(int(st.alert), 0);
    QVERIFY(QString::fromStdString(st.message).endsWith("\nMail"));
    QCOMPARE(st.timestamp.second, 26);       // phone time at the moment of writing (+30 s)

    watchproto::DecodedFrame a1 = decodeWrite(fa->writes[1]);
    watchproto::DecodedFrame a2 = decodeWrite(fa->writes[2]);
    QCOMPARE(int(a1.alert), 1);
    QCOMPARE(int(a2.alert), 1);
    QCOMPARE(int(a1.category), 4);
    QCOMPARE(int(a2.category), 6);
    QCOMPARE(QString::fromStdString(a1.sender), QString("Messages"));
    QCOMPARE(QString::fromStdString(a1.title), QString("Alex"));
    QCOMPARE(QString::fromStdString(a2.message), QString("Body\nline"));   // normalised
    QVERIFY(a1.id > st.id);
    QVERIFY(a2.id > a1.id);

    // Identical to what watchproto builds for the same notification.
    watchproto::Notification n;
    n.id = a1.id; n.alert = true; n.category = 4;
    n.timestamp = a1.timestamp;
    n.sender = "Messages"; n.title = "Alex"; n.message = "Hello";
    watchproto::NotifFrame f = watchproto::buildNotificationFrame(watchproto::fitToWatch(n));
    QVERIFY(f.ok());
    QCOMPARE(fa->writes[1].value, QByteArray(reinterpret_cast<const char *>(f.bytes.data()),
                                             int(f.bytes.size())));
}

void TstWatchlink::syncAndAlertsTogether()
{
    WatchEntry e = entry("AA:AA:AA:AA:AA:01");
    link->enqueueAlert(alertOf("Messages", "Alex", "Hi"), QList<WatchEntry>() << e);
    SessionResult r = link->runSession(req(e, true));
    QVERIFY(r.ok());
    QVERIFY(r.timeSent);
    QCOMPARE(r.alertsSent, 1);
    QCOMPARE(fa->writes.size(), 3);
    QCOMPARE(fa->writes[0].uuid.left(8), QString("26eb002d"));  // time first
    QVERIFY(fa->writes[0].withResponse);
    QCOMPARE(int(decodeWrite(fa->writes[1]).alert), 0);           // then the status
    QCOMPARE(int(decodeWrite(fa->writes[2]).alert), 1);           // the alert is the newest
}

void TstWatchlink::writesAreSpaced()
{
    // Phone test: 8 frames written 3 ms apart, only 2 arrived. Default pacing.
    link->setWriteGapMs(kWriteGapMs);
    WatchEntry e = entry("AA:AA:AA:AA:AA:01");
    for (int i = 0; i < 3; ++i)
        link->enqueueAlert(alertOf("App", "t", "m"), QList<WatchEntry>() << e);
    QVERIFY(link->runSession(req(e, true)).ok());
    // time, status, alert x3: a pause before every 0030 write after the first write.
    QStringList w = fa->calls.filter(QRegExp("^(write|settle)"));
    QCOMPARE(w, QStringList() << "write 26eb002d req" << "settle" << "write 26eb0030 cmd"
                              << "settle" << "write 26eb0030 cmd" << "settle"
                              << "write 26eb0030 cmd" << "settle" << "write 26eb0030 cmd"
                              << "settle");   // the last one is the pause before the disconnect
}

void TstWatchlink::nothingQueuedDoesNotConnect()
{
    SessionResult r = link->runSession(req(entry("AA:AA:AA:AA:AA:01")));
    QVERIFY(r.ok());
    QVERIFY(r.skipped);
    QVERIFY(!r.connected);
    QVERIFY(fa->calls.isEmpty());
}

void TstWatchlink::queueLimitDropsOldest()
{
    WatchEntry e = entry("AA:AA:AA:AA:AA:01");
    int dropped = 0;
    for (int i = 1; i <= 25; ++i)
        dropped += link->enqueueAlert(alertOf("App", QByteArray::number(i).constData(), "x"),
                                      QList<WatchEntry>() << e);
    QCOMPARE(dropped, 15);
    QCOMPARE(link->queued(e.key), 10);
    QCOMPARE(kMaxQueued, 10);
    SessionResult r = link->runSession(req(e));
    QCOMPARE(r.alertsSent, 10);
    // writes[0] is the status; the first alert is number 16 (circular: the oldest went).
    QCOMPARE(QString::fromStdString(decodeWrite(fa->writes[1]).title), QString("16"));
    QCOMPARE(QString::fromStdString(decodeWrite(fa->writes[10]).title), QString("25"));
}

void TstWatchlink::queueAgeing()
{
    WatchEntry e = entry("AA:AA:AA:AA:AA:01");
    link->enqueueAlert(alertOf("App", "old", "x"), QList<WatchEntry>() << e);
    clock->t = clock->t.addSecs(100);
    link->enqueueAlert(alertOf("App", "mid", "x"), QList<WatchEntry>() << e);
    clock->t = clock->t.addSecs(200);      // old: 300 s (kept), mid: 200 s
    clock->t = clock->t.addSecs(1);        // old: 301 s (dropped)
    SessionResult r = link->runSession(req(e));
    QVERIFY(r.ok());
    QCOMPARE(r.alertsSent, 1);
    QCOMPARE(r.alertsDropped, 1);
    QCOMPARE(QString::fromStdString(decodeWrite(fa->writes[1]).title), QString("mid"));

    // Exactly 5 minutes is not older than 5 minutes.
    fa->writes.clear();
    link->enqueueAlert(alertOf("App", "edge", "x"), QList<WatchEntry>() << e);
    clock->t = clock->t.addSecs(kMaxAgeSeconds);
    r = link->runSession(req(e));
    QCOMPARE(r.alertsSent, 1);

    // All aged: nothing to send, no connection.
    fa->calls.clear();
    link->enqueueAlert(alertOf("App", "late", "x"), QList<WatchEntry>() << e);
    clock->t = clock->t.addSecs(kMaxAgeSeconds + 1);
    r = link->runSession(req(e));
    QVERIFY(r.skipped);
    QCOMPARE(r.alertsDropped, 1);
    QVERIFY(fa->calls.isEmpty());
}

void TstWatchlink::alertsGoToEveryEnabledWatch()
{
    QList<WatchEntry> list;
    list << entry("A1") << entry("A2", "CASIO DW-H5600", false) << entry("A3");
    link->enqueueAlert(alertOf("App", "t", "m"), list);
    QCOMPARE(link->queued("A1"), 1);
    QCOMPARE(link->queued("A2"), 0);     // disabled
    QCOMPARE(link->queued("A3"), 1);
    link->clearQueue("A1");
    QCOMPARE(link->queued("A1"), 0);
}

void TstWatchlink::outOfRangeKeepsAlertsAndRetries()
{
    WatchEntry e = entry("AA:AA:AA:AA:AA:01");
    fa->failConnect.insert("/dev/A", "Host is down");
    link->enqueueAlert(alertOf("App", "t", "m"), QList<WatchEntry>() << e);
    link->enqueueAlert(alertOf("App", "t2", "m"), QList<WatchEntry>() << e);
    const QDateTime t0 = clock->t;
    SessionResult r = link->runSession(req(e));
    QVERIFY(!r.ok());
    QVERIFY(!r.connected);
    QCOMPARE(r.alertsSent, 0);
    QCOMPARE(r.alertsDropped, 0);
    QCOMPARE(r.alertsKept, 2);                       // kept in memory
    QVERIFY(r.error.contains("Host is down"));
    QCOMPARE(link->queued(e.key), 2);
    QCOMPARE(link->nextAttempt(e.key), t0.addSecs(30));
    QCOMPARE(fa->calls.filter(QRegExp("^connect ")).size(), 1);

    // Before the retry time: no connection.
    clock->t = t0.addSecs(29);
    r = link->runSession(req(e));
    QVERIFY(r.skipped);
    QCOMPARE(fa->calls.filter(QRegExp("^connect ")).size(), 1);
    QCOMPARE(link->queued(e.key), 2);

    // Retry: fails again, the interval grows (30, 60, 120, 240, 240 ...).
    const int expected[] = {60, 120, 240, 240, 240};
    clock->t = t0.addSecs(30);
    for (int i = 0; i < 5; ++i) {
        r = link->runSession(req(e));
        QVERIFY(!r.connected);
        QCOMPARE(link->nextAttempt(e.key), clock->t.addSecs(expected[i]));
        clock->t = link->nextAttempt(e.key);
        // the alerts age out after 5 minutes: stop before that
        if (clock->t >= t0.addSecs(kMaxAgeSeconds))
            break;
    }

    // The watch comes back: both alerts are delivered in order, state reset.
    fa->failConnect.clear();
    clock->t = t0.addSecs(20);       // young again for this check
    link->clearQueue(e.key);
    fa->failConnect.insert("/dev/A", "Host is down");
    link->enqueueAlert(alertOf("App", "one", "m"), QList<WatchEntry>() << e);
    link->enqueueAlert(alertOf("App", "two", "m"), QList<WatchEntry>() << e);
    QVERIFY(!link->runSession(req(e)).connected);
    fa->failConnect.clear();
    clock->t = link->nextAttempt(e.key);
    fa->writes.clear();
    r = link->runSession(req(e));
    QVERIFY2(r.ok(), qPrintable(r.error));
    QCOMPARE(r.alertsSent, 2);
    QCOMPARE(QString::fromStdString(decodeWrite(fa->writes[1]).title), QString("one"));
    QCOMPARE(QString::fromStdString(decodeWrite(fa->writes[2]).title), QString("two"));
    QCOMPARE(link->queued(e.key), 0);
    QVERIFY(!link->nextAttempt(e.key).isValid());

    // A new failure starts again at 30 s.
    fa->failConnect.insert("/dev/A", "Host is down");
    link->enqueueAlert(alertOf("App", "three", "m"), QList<WatchEntry>() << e);
    r = link->runSession(req(e));
    QCOMPARE(link->nextAttempt(e.key), clock->t.addSecs(30));
}

void TstWatchlink::retriedAlertsAgeOut()
{
    WatchEntry e = entry("AA:AA:AA:AA:AA:01");
    fa->failConnect.insert("/dev/A", "Host is down");
    link->enqueueAlert(alertOf("App", "old", "m"), QList<WatchEntry>() << e);
    QVERIFY(!link->runSession(req(e)).connected);
    fa->failConnect.clear();
    clock->t = clock->t.addSecs(kMaxAgeSeconds + 1);    // never sent late
    SessionResult r = link->runSession(req(e));
    QVERIFY(r.skipped);
    QCOMPARE(r.alertsDropped, 1);
    QCOMPARE(link->queued(e.key), 0);
}

void TstWatchlink::connectedButNotDeliveredIsLost()
{
    WatchEntry e = entry("AA:AA:AA:AA:AA:01");
    for (int i = 0; i < 3; ++i)
        link->enqueueAlert(alertOf("App", "t", "m"), QList<WatchEntry>() << e);
    fa->failStep = "notify";                // connected, then it goes wrong
    SessionResult r = link->runSession(req(e));
    QVERIFY(!r.ok());
    QVERIFY(r.connected);
    QCOMPARE(r.alertsKept, 0);
    QCOMPARE(r.alertsDropped, 3);
    QCOMPARE(link->queued(e.key), 0);
    QVERIFY(!link->nextAttempt(e.key).isValid());
}

void TstWatchlink::retryQueueIsCircularAndSyncIsNotHeldBack()
{
    WatchEntry e = entry("AA:AA:AA:AA:AA:01");
    fa->failConnect.insert("/dev/A", "Host is down");
    for (int i = 1; i <= 8; ++i)
        link->enqueueAlert(alertOf("App", QByteArray::number(i).constData(), "m"),
                           QList<WatchEntry>() << e);
    QVERIFY(!link->runSession(req(e)).connected);
    for (int i = 9; i <= 12; ++i)         // arrive while waiting: 12 > 10, the oldest go
        link->enqueueAlert(alertOf("App", QByteArray::number(i).constData(), "m"),
                           QList<WatchEntry>() << e);
    QCOMPARE(link->queued(e.key), 10);
    // A time-sync session is not held back by the alert retry time.
    fa->failConnect.clear();
    SessionResult r = link->runSession(req(e, true));
    QVERIFY2(r.ok(), qPrintable(r.error));
    QVERIFY(r.timeSent);
    QCOMPARE(r.alertsSent, 10);
    QCOMPARE(QString::fromStdString(decodeWrite(fa->writes[2]).title), QString("3"));
    QCOMPARE(QString::fromStdString(decodeWrite(fa->writes[11]).title), QString("12"));
    QCOMPARE(retryDelaySeconds(0), 0);
    QCOMPARE(retryDelaySeconds(1), 30);
    QCOMPARE(retryDelaySeconds(2), 60);
    QCOMPARE(retryDelaySeconds(3), 120);
    QCOMPARE(retryDelaySeconds(4), 240);
    QCOMPARE(retryDelaySeconds(5), 240);
    QCOMPARE(retryDelaySeconds(9), 240);
}

void TstWatchlink::connectTimeoutPath()
{
    fa->failConnect.insert("/dev/A", "connect timeout after 20 s");
    SessionResult r = link->runSession(req(entry("AA:AA:AA:AA:AA:01"), true));
    QVERIFY(!r.ok());
    QVERIFY(!r.connected);
    QVERIFY(r.error.contains("timeout"));
    QCOMPARE(fa->connectTimeoutSeen, 20000);
    QVERIFY(fa->writes.isEmpty());
    QVERIFY(!fa->calls.contains("settle"));
}

void TstWatchlink::bluezErrorsAreFailures()
{
    const char *steps[] = {"resolved", "notify"};
    for (unsigned i = 0; i < sizeof(steps) / sizeof(steps[0]); ++i) {
        fa->calls.clear();
        fa->writes.clear();
        fa->failStep = steps[i];
        SessionResult r = link->runSession(req(entry("AA:AA:AA:AA:AA:01"), true));
        QVERIFY(!r.ok());
        QVERIFY(r.connected);
        QVERIFY(!r.timeSent);
        QVERIFY(fa->writes.isEmpty());
        QCOMPARE(fa->calls.last(), QString("disconnect /dev/A"));
    }
    fa->failStep.clear();

    // Time write refused by BlueZ: failure, nothing else written, disconnected.
    fa->calls.clear(); fa->writes.clear();
    fa->failWriteAt = 1;
    SessionResult r = link->runSession(req(entry("AA:AA:AA:AA:AA:01"), true));
    QVERIFY(!r.ok());
    QVERIFY(!r.timeSent);
    QCOMPARE(fa->writes.size(), 1);
    QCOMPARE(fa->calls.last(), QString("disconnect /dev/A"));

    // Link-status write refused: the session counts as failed (R-NOTIF-11).
    fa->calls.clear(); fa->writes.clear();
    fa->failWriteAt = 2;
    r = link->runSession(req(entry("AA:AA:AA:AA:AA:01"), true));
    QVERIFY(!r.ok());
    QVERIFY(r.timeSent);
    QCOMPARE(fa->writes.size(), 2);

    // BlueZ unreachable at the lookup.
    fa->failWriteAt = 0;
    fa->failDevices = true;
    r = link->runSession(req(entry("AA:AA:AA:AA:AA:01"), true));
    QVERIFY(!r.ok());
    QVERIFY(!r.connected);
}

void TstWatchlink::alertWriteFailureStopsSession()
{
    WatchEntry e = entry("AA:AA:AA:AA:AA:01");
    for (int i = 0; i < 4; ++i)
        link->enqueueAlert(alertOf("App", "t", "m"), QList<WatchEntry>() << e);
    fa->failWriteAt = 3;     // status ok, alert 1 ok, alert 2 fails
    SessionResult r = link->runSession(req(e));
    QVERIFY(!r.ok());
    QCOMPARE(r.alertsSent, 1);
    QCOMPARE(r.alertsDropped, 3);
    QCOMPARE(fa->calls.last(), QString("disconnect /dev/A"));
    QCOMPARE(link->queued(e.key), 0);
}

void TstWatchlink::invalidClockIsRefused()
{
    clock->t = QDateTime(QDate(1999, 12, 31), QTime(23, 0, 0), Qt::LocalTime);
    SessionResult r = link->runSession(req(entry("AA:AA:AA:AA:AA:01"), true));
    QVERIFY(!r.ok());
    QVERIFY(!r.timeSent);
    QVERIFY(fa->writes.isEmpty());       // R-TIME-6: nothing malformed is sent
    QCOMPARE(fa->calls.last(), QString("disconnect /dev/A"));
}

void TstWatchlink::listOrderAndNeverConcurrent()
{
    fa->devs << dev("/dev/B", "AA:AA:AA:AA:AA:02") << dev("/dev/C", "AA:AA:AA:AA:AA:03");
    QList<WatchEntry> list;
    list << entry("AA:AA:AA:AA:AA:03") << entry("AA:AA:AA:AA:AA:02", "CASIO DW-H5600", false)
         << entry("AA:AA:AA:AA:AA:01");
    link->enqueueAlert(alertOf("App", "t", "m"), list);
    QList<SessionRequest> reqs;
    for (int i = 0; i < list.size(); ++i)
        reqs << req(list.at(i), true);
    QList<SessionResult> rs = link->runAll(reqs);
    QCOMPARE(rs.size(), 2);                           // the disabled one is skipped
    QCOMPARE(rs[0].key, QString("AA:AA:AA:AA:AA:03"));
    QCOMPARE(rs[1].key, QString("AA:AA:AA:AA:AA:01"));
    QVERIFY(rs[0].ok() && rs[1].ok());
    QCOMPARE(rs[0].alertsSent, 1);                    // the same alert goes to both
    QCOMPARE(rs[1].alertsSent, 1);
    QStringList order = fa->calls.filter(QRegExp("^(connect|disconnect) "));
    QCOMPARE(order, QStringList() << "connect /dev/C" << "disconnect /dev/C"
                                  << "connect /dev/A" << "disconnect /dev/A");
    QCOMPARE(fa->maxConnected, 1);

    // A failing watch does not hold up the next one.
    fa->failConnect.insert("/dev/C", "Host is down");
    link->enqueueAlert(alertOf("App", "t", "m"), list);
    rs = link->runAll(reqs);
    QVERIFY(!rs[0].ok());
    QVERIFY(rs[1].ok());
    QCOMPARE(rs[1].alertsSent, 1);
}

void TstWatchlink::busyGuard()
{
    WatchEntry e = entry("AA:AA:AA:AA:AA:01");
    link->enqueueAlert(alertOf("App", "t", "m"), QList<WatchEntry>() << e);
    reentrant = true;                     // a second session is asked for from inside the first
    SessionResult r = link->runSession(req(e));
    reentrant = false;
    QVERIFY(r.ok());
    QVERIFY(!reentrantResult.ok());
    QVERIFY(!reentrantResult.connected);
    QCOMPARE(fa->calls.filter(QRegExp("^connect ")).size(), 1);
    // After the session the link is free again.
    link->enqueueAlert(alertOf("App", "t", "m"), QList<WatchEntry>() << e);
    QVERIFY(link->runSession(req(e)).ok());
}

void TstWatchlink::deviceLookup()
{
    // Unpaired devices and unknown models are not watches.
    fa->devs << dev("/dev/U", "AA:AA:AA:AA:AA:07", "CASIO DW-H5600", false)
             << dev("/dev/P", "AA:AA:AA:AA:AA:08", "Some Phone");
    QString err;
    QList<FoundWatch> found = link->findWatches(&err);
    QCOMPARE(found.size(), 1);
    QCOMPARE(found[0].device.path, QString("/dev/A"));
    QCOMPARE(found[0].model->id, QString("dw-h5600"));

    // Address recreated by BlueZ: the single paired device with that name is used.
    SessionResult r = link->runSession(req(entry("AA:AA:AA:AA:AA:99"), true));
    QVERIFY(r.ok());
    QCOMPARE(r.deviceAddress, QString("AA:AA:AA:AA:AA:01"));

    // Lower-case address still matches.
    fa->devs << dev("/dev/B", "AA:AA:AA:AA:AA:02");
    fa->calls.clear();
    r = link->runSession(req(entry("aa:aa:aa:aa:aa:02"), true));
    QVERIFY(r.ok());
    QVERIFY(fa->calls.contains("connect /dev/B"));

    // Two paired watches of the same name and an unknown address: refused, no connection.
    fa->calls.clear();
    r = link->runSession(req(entry("AA:AA:AA:AA:AA:99"), true));
    QVERIFY(!r.ok());
    QVERIFY(!r.connected);
    QVERIFY(fa->calls.filter(QRegExp("^connect ")).isEmpty());

    // Not paired any more.
    fa->devs.clear();
    fa->devs << dev("/dev/U", "AA:AA:AA:AA:AA:01", "CASIO DW-H5600", false);
    r = link->runSession(req(entry("AA:AA:AA:AA:AA:01"), true));
    QVERIFY(!r.ok());
    QVERIFY(!r.connected);
}

void TstWatchlink::identityCheck()
{
    // Feature answer from: 10 07 06 05 04 03 02 01...; address reversed.
    SessionRequest q = req(entry("AA:AA:AA:AA:AA:01"), true);
    q.readBleAddress = true;
    SessionResult r = link->runSession(q);
    QVERIFY(r.ok());
    QCOMPARE(r.bleAddress, QString("01:02:03:04:05:06"));
    QVERIFY(fa->calls.contains("read 10"));

    // Same address expected (any case): fine.
    fa->calls.clear(); fa->writes.clear();
    q.expectedBleAddress = "01:02:03:04:05:06";
    r = link->runSession(q);
    QVERIFY(r.ok());
    QCOMPARE(fa->writes.size(), 2);

    // Another watch: failure, nothing written, disconnected.
    fa->calls.clear(); fa->writes.clear();
    q.expectedBleAddress = "11:22:33:44:55:66";
    r = link->runSession(q);
    QVERIFY(!r.ok());
    QVERIFY(r.connected);
    QVERIFY(fa->writes.isEmpty());
    QCOMPARE(fa->calls.last(), QString("disconnect /dev/A"));

    // Expected but the answer never comes: cannot verify, failure.
    fa->calls.clear();
    fa->featureAnswerOk = false;
    r = link->runSession(q);
    QVERIFY(!r.ok());
    QVERIFY(fa->writes.isEmpty());

    // Nothing expected and the read fails: carry on.
    q.expectedBleAddress.clear();
    fa->calls.clear();
    r = link->runSession(q);
    QVERIFY(r.ok());
    QVERIFY(r.bleAddress.isEmpty());
    QVERIFY(r.timeSent);

    // No read at all when neither is wanted.
    fa->calls.clear();
    r = link->runSession(req(entry("AA:AA:AA:AA:AA:01"), true));
    QVERIFY(!fa->calls.contains("read 10"));
}

void TstWatchlink::bleAddressParsing()
{
    QCOMPARE(WatchLink::bleAddressFromFeature10(feature10("060504030201")),
             QString("01:02:03:04:05:06"));
    QCOMPARE(WatchLink::bleAddressFromFeature10(QByteArray::fromHex("ff8109")), QString());
    QCOMPARE(WatchLink::bleAddressFromFeature10(QByteArray::fromHex("1074c3")), QString());
    QCOMPARE(WatchLink::bleAddressFromFeature10(QByteArray()), QString());
}

QTEST_APPLESS_MAIN(TstWatchlink)
#include "tst_watchlink.moc"
