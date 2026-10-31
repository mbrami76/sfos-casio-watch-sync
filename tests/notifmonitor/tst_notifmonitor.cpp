// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Tests for notifmonitor: the pure parser, the constants, and BecomeMonitor
// against a private dbus-daemon (skipped when the daemon cannot be started).
#include "notifmonitor.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>

using namespace notifmonitor;
using notiffilter::IncomingNotification;

// Stands in for the notification service so that Notify calls have a
// destination.
class FakeNotifyd : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")
public slots:
    uint Notify(const QString &, uint, const QString &, const QString &, const QString &,
                const QStringList &, const QVariantMap &, int)
    {
        return 1;
    }
    QStringList GetCapabilities() { return QStringList(); }
};

class TstNotifMonitor : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QProcess m_daemon;
    QByteArray m_address;
    FakeNotifyd m_fake;

    static QVariantList fullArgs()
    {
        QVariantMap hints;
        hints.insert("urgency", QVariant::fromValue<uchar>(1));
        hints.insert("x-nemo-owner", QString("owner"));
        QVariantList a;
        a << QString("Messages") << QVariant(7u) << QString("icon") << QString("Summary")
          << QString("Body text") << QStringList() << hints << QVariant(-1);
        return a;
    }

    bool startDaemon(bool denyMonitoring)
    {
        const QString conf = m_dir.path() + "/bus.conf";
        const QString sock = m_dir.path() + "/bus";
        QFile f(conf);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        QTextStream t(&f);
        t << "<busconfig><type>session</type>"
          << "<listen>unix:path=" << sock << "</listen><auth>EXTERNAL</auth>"
          << "<policy context=\"default\"><allow send_destination=\"*\" eavesdrop=\"true\"/>"
          << "<allow eavesdrop=\"true\"/><allow own=\"*\"/>";
        if (denyMonitoring)
            t << "<deny send_destination=\"org.freedesktop.DBus\""
                 " send_interface=\"org.freedesktop.DBus.Monitoring\"/>";
        t << "</policy></busconfig>\n";
        f.close();
        QFile::remove(sock);
        m_address = QByteArray("unix:path=") + sock.toUtf8();
        m_daemon.start("dbus-daemon", QStringList() << "--config-file" << conf << "--nofork");
        if (!m_daemon.waitForStarted(3000))
            return false;
        for (int i = 0; i < 100 && !QFile::exists(sock); ++i)
            QTest::qWait(50);
        return QFile::exists(sock);
    }

    void stopDaemon()
    {
        if (m_daemon.state() != QProcess::NotRunning) {
            m_daemon.terminate();
            if (!m_daemon.waitForFinished(3000)) {
                m_daemon.kill();
                m_daemon.waitForFinished(3000);
            }
        }
    }

    // Calls Notify from a second connection; returns after the call is sent.
    void sendNotify(QDBusConnection &client, const QString &app, uint replaces,
                    const QString &summary, const QString &body,
                    const QVariantMap &hints = QVariantMap())
    {
        QDBusMessage m = QDBusMessage::createMethodCall(
            "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
            "org.freedesktop.Notifications", "Notify");
        m << app << QVariant::fromValue<quint32>(replaces) << QString() << summary << body
          << QStringList() << hints << QVariant::fromValue<qint32>(-1);
        client.send(m);
    }

private slots:
    void initTestCase() { qRegisterMetaType<IncomingNotification>("notiffilter::IncomingNotification"); }

    void cleanup()
    {
        QDBusConnection::disconnectFromBus("tst-server");
        QDBusConnection::disconnectFromBus("tst-client");
        stopDaemon();
    }

    // ---- constants ------------------------------------------------------

    void ownAppName_isDroppedByFilter()
    {
        QVERIFY(!ownAppName().trimmed().isEmpty());
        notiffilter::NotifSettings s;
        s.masterOn = true;
        s.ownAppName = ownAppName();
        s.enabledApps.insert(ownAppName());
        IncomingNotification in;
        in.appName = ownAppName();
        in.summary = "x";
        QCOMPARE(notiffilter::filter(in, s).decision, notiffilter::DropOwnApp);
    }

    void retryDelay_sequence()
    {
        const int expected[] = {1000, 2000, 4000, 8000, 16000, 32000, 60000, 60000, 60000};
        for (int i = 0; i < 9; ++i)
            QCOMPARE(retryDelayMs(i), expected[i]);
        QCOMPARE(retryDelayMs(-5), 1000);
        QCOMPARE(retryDelayMs(1000000), 60000);
        QCOMPARE(kRetryInitialMs, 1000);
        QCOMPARE(kRetryMaxMs, 60000);
    }

    // ---- parseNotify ----------------------------------------------------

    void parse_full()
    {
        const QDateTime when(QDate(2026, 10, 25), QTime(12, 30, 5));
        const IncomingNotification n = parseNotify(fullArgs(), when);
        QCOMPARE(n.appName, QString("Messages"));
        QCOMPARE(n.summary, QString("Summary"));
        QCOMPARE(n.body, QString("Body text"));
        QCOMPARE(n.replacesId, 7u);
        QCOMPARE(n.when, when);
        QCOMPARE(n.hints.size(), 2);
        QCOMPARE(n.hints.value("x-nemo-owner").toString(), QString("owner"));
        QCOMPARE(n.hints.value("urgency").toInt(), 1);
    }

    void parse_missingAndShortArguments()
    {
        IncomingNotification n = parseNotify(QVariantList(), QDateTime());
        QVERIFY(n.appName.isEmpty() && n.summary.isEmpty() && n.body.isEmpty());
        QCOMPARE(n.replacesId, 0u);
        QVERIFY(n.hints.isEmpty());

        QVariantList few;
        few << QString("App") << QVariant(3u);
        n = parseNotify(few, QDateTime());
        QCOMPARE(n.appName, QString("App"));
        QCOMPARE(n.replacesId, 3u);
        QVERIFY(n.summary.isEmpty() && n.body.isEmpty() && n.hints.isEmpty());

        QVariantList noHints = fullArgs();
        noHints.removeAt(7);
        noHints.removeAt(6);
        n = parseNotify(noHints, QDateTime());
        QCOMPARE(n.body, QString("Body text"));
        QVERIFY(n.hints.isEmpty());
    }

    void parse_wrongTypes()
    {
        QVariantList a;
        a << 42 << QString("x") << 7.5 << QStringList("s") << QVariant(true)
          << QString("a") << QString("not a map") << QString("z");
        IncomingNotification n = parseNotify(a, QDateTime());
        QVERIFY(n.appName.isEmpty());
        QCOMPARE(n.replacesId, 0u);
        QVERIFY(n.summary.isEmpty());
        QVERIFY(n.body.isEmpty());
        QVERIFY(n.hints.isEmpty());

        QVariantList b;
        b << QByteArray("bytes") << QVariant() << QVariant() << QVariant() << QVariant()
          << QVariant() << QVariant() << QVariant();
        n = parseNotify(b, QDateTime());
        QVERIFY(n.appName.isEmpty() && n.summary.isEmpty() && n.body.isEmpty());
        QCOMPARE(n.replacesId, 0u);
    }

    void parse_emptyStrings()
    {
        QVariantList a;
        a << QString() << QVariant(0u) << QString() << QString() << QString() << QStringList()
          << QVariantMap() << QVariant(0);
        const IncomingNotification n = parseNotify(a, QDateTime());
        QVERIFY(n.appName.isEmpty() && n.summary.isEmpty() && n.body.isEmpty());
        QVERIFY(n.hints.isEmpty());
    }

    void parse_replacesId()
    {
        struct Row { QVariant v; uint expected; };
        const Row rows[] = {
            {QVariant(0u), 0u},
            {QVariant(42u), 42u},
            {QVariant(4294967295u), 4294967295u},
            {QVariant(7), 7u},
            {QVariant(-1), 0u},
            {QVariant(Q_INT64_C(5000000000)), 0u},
            {QVariant(Q_UINT64_C(4294967295)), 4294967295u},
            {QVariant(Q_UINT64_C(4294967296)), 0u},
            {QVariant::fromValue<ushort>(9), 9u},
            {QVariant::fromValue<short>(-3), 0u},
            {QVariant::fromValue<uchar>(3), 3u},
            {QVariant(2.0), 0u},
            {QVariant(true), 0u},
            {QVariant(QString("5")), 0u},
        };
        for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
            QVariantList a = fullArgs();
            a[1] = rows[i].v;
            QCOMPARE(parseNotify(a, QDateTime()).replacesId, rows[i].expected);
        }
    }

    void parse_multiByteAndLongText()
    {
        const QString multi = QString::fromUtf8("Hiya \xc3\xa0 \xe2\x82\xac \xf0\x9f\x98\x80 \xe6\x97\xa5\xe6\x9c\xac\n\ttab ");
        QVariantList a = fullArgs();
        a[3] = multi;
        a[4] = multi + multi;
        IncomingNotification n = parseNotify(a, QDateTime());
        QCOMPARE(n.summary, multi);       // unchanged: no trimming, no normalising
        QCOMPARE(n.body, multi + multi);

        const QString big(200000, QLatin1Char('x'));
        a[3] = big;
        a[4] = big + QString::fromUtf8("\xf0\x9f\x98\x80");
        n = parseNotify(a, QDateTime());
        QCOMPARE(n.summary.size(), 200000);
        QCOMPARE(n.body, big + QString::fromUtf8("\xf0\x9f\x98\x80"));
    }

    void parse_hintsKeptAsTheyAre()
    {
        QVariantMap h;
        h.insert("x-nemo-visibility", QString("private"));
        h.insert("category", QString("email"));
        h.insert("wrapped", QVariant::fromValue(QDBusVariant(QVariant(5))));
        h.insert("nested", QVariantMap());
        QVariantList a = fullArgs();
        a[6] = h;
        IncomingNotification n = parseNotify(a, QDateTime());
        QCOMPARE(n.hints.size(), 4);
        QCOMPARE(n.hints.value("x-nemo-visibility").toString(), QString("private"));
        QCOMPARE(n.hints.value("category").toString(), QString("email"));
        QCOMPARE(n.hints.value("wrapped").toInt(), 5); // QDBusVariant unwrapped
        QVERIFY(n.hints.contains("nested"));

        QVariantHash hh;
        hh.insert("k", QString("v"));
        a[6] = hh;
        n = parseNotify(a, QDateTime());
        QCOMPARE(n.hints.value("k").toString(), QString("v"));
    }

    void parse_replacesIdDoesNotChangeOutputShape()
    {
        // Same call with replaces_id 0 and 12: identical except replacesId.
        QVariantList a = fullArgs();
        a[1] = QVariant(0u);
        const IncomingNotification n0 = parseNotify(a, QDateTime());
        a[1] = QVariant(12u);
        const IncomingNotification n12 = parseNotify(a, QDateTime());
        QCOMPARE(n0.replacesId, 0u);
        QCOMPARE(n12.replacesId, 12u);
        QCOMPARE(n0.appName, n12.appName);
        QCOMPARE(n0.summary, n12.summary);
        QCOMPARE(n0.body, n12.body);
        QCOMPARE(n0.hints, n12.hints);
    }

    // ---- BecomeMonitor against a private dbus-daemon ----------------------

    void bus_monitorReceivesEveryNotify()
    {
        if (!startDaemon(false))
            QSKIP("dbus-daemon cannot be started here");
        QDBusConnection server = QDBusConnection::connectToBus(QString::fromLatin1(m_address), "tst-server");
        QDBusConnection client = QDBusConnection::connectToBus(QString::fromLatin1(m_address), "tst-client");
        QVERIFY(server.isConnected() && client.isConnected());
        QVERIFY(server.registerService("org.freedesktop.Notifications"));
        QVERIFY(server.registerObject("/org/freedesktop/Notifications", &m_fake,
                                      QDBusConnection::ExportAllSlots));

        NotifMonitor mon(m_address);
        QCOMPARE(int(mon.state()), int(NotifMonitor::NotRunning));
        QSignalSpy got(&mon, SIGNAL(notificationReceived(notiffilter::IncomingNotification)));
        QSignalSpy states(&mon, SIGNAL(stateChanged(int,QString)));
        mon.start();
        QCOMPARE(int(mon.state()), int(NotifMonitor::Running));
        QVERIFY(mon.reason().isEmpty());
        QCOMPARE(states.count(), 1);

        const QString text = QString::fromUtf8("Hiya \xc3\xa0 \xf0\x9f\x98\x80");
        QVariantMap hints;
        hints.insert("urgency", QVariant::fromValue<uchar>(2));
        hints.insert("x-nemo-visibility", QString("private"));
        hints.insert("x-nemo-display-on", true);
        hints.insert("count", 7);
        hints.insert("lines", QStringList() << "a" << "b");

        const QDateTime before = QDateTime::currentDateTime().addSecs(-1);
        // A call that does not match the rule must not reach the monitor.
        client.send(QDBusMessage::createMethodCall("org.freedesktop.Notifications",
                    "/org/freedesktop/Notifications", "org.freedesktop.Notifications", "GetCapabilities"));
        sendNotify(client, "Messages", 0, text, text + text, hints);
        QTRY_COMPARE_WITH_TIMEOUT(got.count(), 1, 5000);

        const IncomingNotification n = qvariant_cast<IncomingNotification>(got.at(0).at(0));
        QCOMPARE(n.appName, QString("Messages"));
        QCOMPARE(n.summary, text);
        QCOMPARE(n.body, text + text);
        QCOMPARE(n.replacesId, 0u);
        QVERIFY(n.when >= before && n.when <= QDateTime::currentDateTime().addSecs(1));
        QCOMPARE(n.hints.value("urgency").toInt(), 2);
        QCOMPARE(n.hints.value("x-nemo-visibility").toString(), QString("private"));
        QCOMPARE(n.hints.value("x-nemo-display-on").toBool(), true);
        QCOMPARE(n.hints.value("count").toInt(), 7);
        QCOMPARE(n.hints.value("lines").toStringList(), QStringList() << "a" << "b");

        // replaces_id: every Notify is a new notification, whatever it says.
        sendNotify(client, "Messages", 5, "one", "");
        sendNotify(client, "Messages", 5, "two", "");
        sendNotify(client, "Mail", 5, "three", "");
        QTRY_COMPARE_WITH_TIMEOUT(got.count(), 4, 5000);
        QCOMPARE(qvariant_cast<IncomingNotification>(got.at(1).at(0)).replacesId, 5u);
        QCOMPARE(qvariant_cast<IncomingNotification>(got.at(2).at(0)).summary, QString("two"));
        QCOMPARE(qvariant_cast<IncomingNotification>(got.at(3).at(0)).appName, QString("Mail"));

        // The monitor never sent anything: the bus did not close it, and the
        // state did not change.
        QTest::qWait(300);
        sendNotify(client, "Late", 0, "again", "");
        QTRY_COMPARE_WITH_TIMEOUT(got.count(), 5, 5000);
        QCOMPARE(int(mon.state()), int(NotifMonitor::Running));
        QCOMPARE(states.count(), 1);
    }

    void bus_accessDeniedIsCannotMonitorAndNotRetried()
    {
        if (!startDaemon(true))
            QSKIP("dbus-daemon cannot be started here");
        NotifMonitor mon(m_address);
        QSignalSpy states(&mon, SIGNAL(stateChanged(int,QString)));
        mon.start();
        QCOMPARE(int(mon.state()), int(NotifMonitor::CannotMonitor));
        QVERIFY(!mon.reason().isEmpty());
        QCOMPARE(states.count(), 1);
        QTest::qWait(2500); // longer than the first retry delay
        QCOMPARE(int(mon.state()), int(NotifMonitor::CannotMonitor));
        QCOMPARE(states.count(), 1);
    }

    void bus_lostConnectionIsRetried()
    {
        if (!startDaemon(false))
            QSKIP("dbus-daemon cannot be started here");
        NotifMonitor mon(m_address);
        QSignalSpy states(&mon, SIGNAL(stateChanged(int,QString)));
        mon.start();
        QCOMPARE(int(mon.state()), int(NotifMonitor::Running));

        stopDaemon();
        QTRY_COMPARE_WITH_TIMEOUT(int(mon.state()), int(NotifMonitor::NotRunning), 5000);
        QVERIFY(!mon.reason().isEmpty());

        // The bus comes back: the monitor returns after its retry delay.
        QVERIFY(startDaemon(false));
        QTRY_COMPARE_WITH_TIMEOUT(int(mon.state()), int(NotifMonitor::Running), 15000);

        mon.stop();
        QCOMPARE(int(mon.state()), int(NotifMonitor::NotRunning));
    }

    void bus_noBusIsNotRunningAndRetries()
    {
        NotifMonitor mon(QByteArray("unix:path=/nonexistent/notifmonitor-test-bus"));
        QSignalSpy states(&mon, SIGNAL(stateChanged(int,QString)));
        mon.start();
        QCOMPARE(int(mon.state()), int(NotifMonitor::NotRunning));
        QVERIFY(!mon.reason().isEmpty());
        mon.stop(); // must cancel the pending retry
        QTest::qWait(1500);
        QCOMPARE(int(mon.state()), int(NotifMonitor::NotRunning));
    }
};

QTEST_MAIN(TstNotifMonitor)
#include "tst_notifmonitor.moc"
