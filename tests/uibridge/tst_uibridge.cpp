// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Tests of the UI bridge: silhouette rule, list models,
// settings bridge, text helpers, mapping of GetStatus / ListWatches, the D-Bus
// client against a fake peer on a private bus, and the single-instance guard.
#include "appinfo.h"
#include "appsmodel.h"
#include "formatter.h"
#include "instanceguard.h"
#include "serviceclient.h"
#include "settingsbridge.h"
#include "silhouettecatalog.h"
#include "uibridge.h"
#include "watchesmodel.h"

#include <QDBusAbstractAdaptor>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMetaType>
#include <QDirIterator>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QtTest>

using namespace uibridge;

namespace {

// ------------------------------------------------------------- private bus

class PrivateBus
{
public:
    bool start(const QString &dir)
    {
        const QString conf = dir + "/bus.conf";
        const QString sock = dir + "/bus";
        QFile f(conf);
        if (!f.open(QIODevice::WriteOnly))
            return false;
        QTextStream t(&f);
        t << "<busconfig><type>session</type><listen>unix:path=" << sock
          << "</listen><auth>EXTERNAL</auth><policy context=\"default\">"
             "<allow send_destination=\"*\" eavesdrop=\"true\"/><allow eavesdrop=\"true\"/>"
             "<allow own=\"*\"/></policy></busconfig>\n";
        t.flush();
        f.close();
        m_daemon.start("dbus-daemon", QStringList() << "--config-file" << conf << "--nofork");
        if (!m_daemon.waitForStarted(3000))
            return false;
        for (int i = 0; i < 100 && !QFile::exists(sock); ++i)
            QTest::qWait(50);
        m_address = "unix:path=" + sock;
        return QFile::exists(sock);
    }
    ~PrivateBus()
    {
        m_daemon.terminate();
        m_daemon.waitForFinished(3000);
    }
    QString address() const { return m_address; }
private:
    QProcess m_daemon;
    QString m_address;
};

// The fake service: the same names and signatures as service/servicebus.
class FakeService : public QObject, protected QDBusContext
{
    Q_OBJECT
public:
    FakeService() : listFails(false), reloadCalls(0), testCalls(0) {}
    QVariantMap status;
    QList<QVariantMap> watches;
    bool listFails;
    QStringList syncKeys;
    int reloadCalls, testCalls;

    QVariantMap getStatus() { return status; }
    bool syncNow(const QString &key) { syncKeys << key; return key != QLatin1String("nope"); }
    QList<QVariantMap> listWatches()
    {
        if (listFails) {
            sendErrorReply(QStringLiteral("it.michelebrami.sfos_casio_sync.Error.Bluetooth"),
                           QStringLiteral("BlueZ cannot be read"));
            return QList<QVariantMap>();
        }
        return watches;
    }
    void changed() { emit statusChanged(); }
signals:
    void statusChanged();
};

class FakeAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "it.michelebrami.sfos_casio_sync")
public:
    explicit FakeAdaptor(FakeService *s) : QDBusAbstractAdaptor(s), m_s(s)
    {
        connect(s, SIGNAL(statusChanged()), this, SIGNAL(StatusChanged()));
    }
public slots:
    QVariantMap GetStatus() { return m_s->getStatus(); }
    bool SyncNow(const QString &key) { return m_s->syncNow(key); }
    void ReloadSettings() { ++m_s->reloadCalls; }
    bool TestNotification() { ++m_s->testCalls; return true; }
    QList<QVariantMap> ListWatches() { return m_s->listWatches(); }
signals:
    void StatusChanged();
private:
    FakeService *m_s;
};

// Runs InstanceGuard::claim() of a second "process" while the main thread
// keeps the event loop of the first one (in reality they are two processes).
class ClaimThread : public QThread
{
public:
    QString address;
    bool result;
    ClaimThread() : result(true) {}
    void run()
    {
        {
            QDBusConnection c = QDBusConnection::connectToBus(address, "tst-uibridge-second");
            InstanceGuard second(c);
            result = second.claim(5000);
        }
        QDBusConnection::disconnectFromBus("tst-uibridge-second");
    }
};

settings::WatchEntry entry(const QString &key, const QString &name, const QString &modelId,
                           const QString &alias = QString(), bool enabled = false,
                           bool ack = false, const QString &silhouette = QString())
{
    settings::WatchEntry e;
    e.key = key;
    e.address = key;
    e.deviceName = name;
    e.modelId = modelId;
    e.alias = alias;
    e.enabled = enabled;
    e.riskAcknowledged = ack;
    e.silhouette = silhouette;
    return e;
}

QVariantMap statusWatch(const QString &key, bool usable, qlonglong lastSuccess, qlonglong next)
{
    QVariantMap m;
    m.insert("key", key);
    m.insert("usable", usable);
    m.insert("lastSuccess", lastSuccess);
    m.insert("nextSync", next);
    m.insert("running", false);
    m.insert("lastError", QString());
    m.insert("problem", QString());
    return m;
}

// "Another process" changes the settings file behind the open QSettings objects
// (the objects of one process share their cache): append a key, and change the size.
void appendToFile(const QString &path, const QString &section, const QString &keyValue)
{
    QFile f(path);
    QVERIFY(f.open(QIODevice::Append));
    f.write(("\n[" + section + "]\n" + keyValue + "\n; padding to change the size\n").toUtf8());
}

QString readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

} // namespace

class TstUiBridge : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    int m_n;

    QString path(const QString &name) { return m_dir.path() + "/" + name; }
    QString fresh(const QString &name) { return path(name + QString::number(++m_n)); }

    // models: DW-H5600 supported (default dw-h5600), GW-B5600 untested (no default),
    // GBD-100 untested (default gbx-100), ODD untested (default that does not exist)
    QString writeModels()
    {
        const QString p = fresh("models") + ".json";
        QFile f(p);
        f.open(QIODevice::WriteOnly);
        f.write("{\"version\":1,\"models\":["
                "{\"id\":\"dw-h5600\",\"label\":\"Casio DW-H5600\",\"family\":\"h5600\","
                "\"status\":\"supported\",\"defaultSilhouette\":\"dw-h5600\","
                "\"namePatterns\":[\"CASIO DW-H5600\"],\"params\":{\"dowEncoding\":\"sun0\"}},"
                "{\"id\":\"gw-b5600\",\"label\":\"Casio GW-B5600\",\"family\":\"b5600\","
                "\"status\":\"untested\",\"namePatterns\":[\"CASIO GW-B5600\"],"
                "\"params\":{\"dowEncoding\":\"sun0\"}},"
                "{\"id\":\"gbd-100\",\"label\":\"Casio GBD-100\",\"family\":\"gbd\","
                "\"status\":\"untested\",\"defaultSilhouette\":\"gbx-100\","
                "\"namePatterns\":[\"CASIO GBD-100\"],\"params\":{\"dowEncoding\":\"sun0\"}},"
                "{\"id\":\"odd\",\"label\":\"Casio ODD\",\"family\":\"odd\","
                "\"status\":\"untested\",\"defaultSilhouette\":\"nonexistent\","
                "\"namePatterns\":[\"CASIO ODD\"],\"params\":{\"dowEncoding\":\"sun0\"}}]}");
        f.close();
        return p;
    }

    struct Fixture {
        settings::Settings settings;
        watchproto::ModelTable models;
        SilhouetteCatalog catalog;
        WatchesModel model;
        Fixture(const QString &settingsPath, const QString &modelsPath, const QString &state)
            : settings(settingsPath), catalog(SILHOUETTES_DIR),
              model(&settings, loadModels(&models, modelsPath), &catalog, state) {}
        static const watchproto::ModelTable *loadModels(watchproto::ModelTable *t, const QString &p)
        {
            settings::loadModelTable(t, p);
            return t;
        }
    };

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_n = 0;
        qDBusRegisterMetaType<QList<QVariantMap> >();
    }

    // ------------------------------------------------------- silhouettes

    void silhouettesLoadFromTheShippedIndex()
    {
        SilhouetteCatalog c(SILHOUETTES_DIR);
        QCOMPARE(c.count(), 5);
        QStringList ids, labels;
        const QList<SilhouetteInfo> all = c.entries();
        for (int i = 0; i < all.size(); ++i) {
            ids << all.at(i).id;
            labels << all.at(i).label;
        }
        QCOMPARE(ids, QStringList() << "classic" << "dw-h5600" << "edifice" << "gbx-100" << "round");
        QCOMPARE(labels, QStringList() << "Classic" << "DW-H5600" << "Edifice" << "GBX-100" << "Round");
        QVERIFY(c.sourceOf("classic").startsWith("file://"));
        QVERIFY(c.sourceOf("classic").endsWith("/classic.svg"));
        QVERIFY(QFile::exists(QUrl(c.sourceOf("round")).toLocalFile()));
        QVERIFY(qAbs(c.aspectOf("classic") - 96.326 / 201.877) < 1e-6);
        QCOMPARE(c.aspectOf("nope"), 0.0);
        QCOMPARE(c.labelOf("edifice"), QString("Edifice"));
        QCOMPARE(c.labelOf("nope"), QString());
        QCOMPARE(c.rowCount(), 5);
        QCOMPARE(c.data(c.index(3), SilhouetteCatalog::IdRole).toString(), QString("gbx-100"));
        QCOMPARE(c.data(c.index(3), SilhouetteCatalog::LabelRole).toString(), QString("GBX-100"));
    }

    void effectiveSilhouetteRule()
    {
        SilhouetteCatalog c(SILHOUETTES_DIR);
        QCOMPARE(c.effectiveId("round", "edifice"), QString("round"));        // own choice wins
        QCOMPARE(c.effectiveId("", "edifice"), QString("edifice"));           // model default
        QCOMPARE(c.effectiveId("unknown-id", "gbx-100"), QString("gbx-100")); // unknown own id
        QCOMPARE(c.effectiveId("", ""), QString("classic"));                  // nothing
        QCOMPARE(c.effectiveId("", "nonexistent"), QString("classic"));       // unknown default
        QCOMPARE(c.effectiveId("nope", "nope2"), QString("classic"));
        SilhouetteCatalog none(path("no-such-folder"));                       // no index at all
        QCOMPARE(none.count(), 0);
        QCOMPARE(none.effectiveId("round", "edifice"), QString("classic"));
    }

    void silhouetteIndexIsValidated()
    {
        const QString dir = fresh("sil");
        QDir().mkpath(dir);
        QFile f(dir + "/silhouettes.json");
        f.open(QIODevice::WriteOnly);
        f.write("{\"silhouettes\":["
                "{\"id\":\"ok\",\"label\":\"Ok\",\"file\":\"ok.svg\",\"viewBox\":[0,0,10,20]},"
                "{\"id\":\"\",\"label\":\"x\",\"file\":\"x.svg\",\"viewBox\":[0,0,10,20]},"
                "{\"id\":\"esc\",\"label\":\"x\",\"file\":\"../x.svg\",\"viewBox\":[0,0,10,20]},"
                "{\"id\":\"sub\",\"label\":\"x\",\"file\":\"a/b.svg\",\"viewBox\":[0,0,10,20]},"
                "{\"id\":\"flat\",\"label\":\"x\",\"file\":\"f.svg\",\"viewBox\":[0,0,10,0]},"
                "{\"id\":\"nobox\",\"label\":\"x\",\"file\":\"n.svg\"},"
                "{\"id\":\"ok\",\"label\":\"dup\",\"file\":\"d.svg\",\"viewBox\":[0,0,1,1]},"
                "{\"id\":\"nolabel\",\"file\":\"l.svg\",\"viewBox\":[0,0,1,2]}]}");
        f.close();
        SilhouetteCatalog c(dir);
        QCOMPARE(c.count(), 2);
        QVERIFY(c.contains("ok"));
        QVERIFY(c.contains("nolabel"));
        QCOMPARE(c.labelOf("nolabel"), QString("nolabel"));   // label falls back to the id
        QCOMPARE(c.labelOf("ok"), QString("Ok"));             // the first of two equal ids
        QFile g(dir + "/silhouettes.json");                   // a corrupt index: empty, no crash
        g.open(QIODevice::WriteOnly | QIODevice::Truncate);
        g.write("not json");
        g.close();
        QVERIFY(!c.load(dir));
        QCOMPARE(c.count(), 0);
    }

    // -------------------------------------------------------------- text

    void formatterTimes()
    {
        const QDateTime now(QDate(2026, 10, 25), QTime(18, 0, 0), Qt::LocalTime);
        struct At {
            static qlonglong at(int y, int m, int d, int hh, int mm)
            {
                return QDateTime(QDate(y, m, d), QTime(hh, mm, 0), Qt::LocalTime).toMSecsSinceEpoch() / 1000;
            }
        };
        QCOMPARE(Formatter::whenText(0, now), QString("Never"));
        QCOMPARE(Formatter::whenText(At::at(2026, 10, 25, 8, 12), now), QString("Today 08:12"));
        QCOMPARE(Formatter::whenText(At::at(2026, 10, 24, 23, 59), now), QString("Yesterday 23:59"));
        QCOMPARE(Formatter::whenText(At::at(2026, 10, 20, 8, 12), now), QString("20 Oct 08:12"));
        QCOMPARE(Formatter::whenText(At::at(2025, 12, 1, 7, 5), now), QString("1 Dec 07:05"));

        QCOMPARE(Formatter::nextText(0, now), QString("not scheduled"));
        QCOMPARE(Formatter::nextText(At::at(2026, 10, 25, 17, 0), now), QString("due now"));
        QCOMPARE(Formatter::nextText(At::at(2026, 10, 25, 19, 30), now), QString("about 19:30 today"));
        QCOMPARE(Formatter::nextText(At::at(2026, 10, 26, 8, 12), now), QString("about 08:12 tomorrow"));
        QCOMPARE(Formatter::nextText(At::at(2026, 10, 28, 8, 12), now), QString("about 28 Oct 08:12"));

        QCOMPARE(Formatter::lastSyncLine(false, 0, now), QString("No watch enabled"));
        QCOMPARE(Formatter::lastSyncLine(true, 0, now), QString("Not synced yet"));
        QCOMPARE(Formatter::lastSyncLine(true, At::at(2026, 10, 25, 8, 12), now),
                 QString("Last sync Today 08:12"));
    }

    void formatterIntervalLabelsCoverTheAllowedList()
    {
        const QList<int> allowed = settings::allowedIntervalMinutes();
        QStringList labels;
        for (int i = 0; i < allowed.size(); ++i) {
            labels << Formatter::intervalLabel(allowed.at(i));
            QVERIFY2(!labels.last().contains("minutes"), qPrintable(labels.last()));
        }
        QCOMPARE(labels, QStringList() << "6 hours" << "12 hours" << "24 hours" << "2 days"
                                       << "7 days" << "2 weeks" << "1 month (30 days)");
    }

    // ------------------------------------------------- D-Bus value mapping

    void parseStatusFromPlainMaps()
    {
        QVariantMap raw;
        raw.insert("serviceState", "running");
        raw.insert("busy", true);
        raw.insert("masterOn", true);
        raw.insert("intervalMinutes", 1440);
        raw.insert("modelTableLoaded", true);
        raw.insert("monitorState", "cannot-monitor");
        raw.insert("monitorReason", "sandboxed");
        raw.insert("riskProblems", QStringList() << "AA:02");
        QVariantList ws;
        ws << statusWatch("AA:01", true, 100, 200) << statusWatch("AA:02", false, 0, 0);
        raw.insert("watches", ws);
        const ServiceStatus s = parseStatus(raw);
        QVERIFY(s.valid);
        QCOMPARE(s.serviceState, QString("running"));
        QVERIFY(s.busy);
        QVERIFY(s.masterOn);
        QCOMPARE(s.intervalMinutes, 1440);
        QCOMPARE(s.monitorState, QString("cannot-monitor"));
        QCOMPARE(s.monitorReason, QString("sandboxed"));
        QCOMPARE(s.riskProblems, QStringList() << "AA:02");
        QCOMPARE(s.watches.size(), 2);
        QCOMPARE(s.watches.at(0).value("key").toString(), QString("AA:01"));
        QCOMPARE(s.watches.at(1).value("usable").toBool(), false);

        // the form the bus delivers: a list of maps held in the variant
        QVariantMap raw2 = raw;
        raw2.insert("watches", QVariant::fromValue(QList<QVariantMap>() << statusWatch("AA:03", true, 1, 2)));
        const ServiceStatus s2 = parseStatus(raw2);
        QCOMPARE(s2.watches.size(), 1);
        QCOMPARE(s2.watches.at(0).value("key").toString(), QString("AA:03"));

        QVERIFY(!parseStatus(QVariantMap()).valid);          // a reply that is not a status
        QVERIFY(toMapList(QVariant()).isEmpty());
        QVERIFY(toStringList(QVariant()).isEmpty());
    }

    // ------------------------------------------------------ settings bridge

    void settingsBridgeRoundTrip()
    {
        const QString sp = fresh("sb") + ".conf";
        SettingsBridge b(sp, fresh("prefs") + ".conf");
        QSignalSpy written(&b, SIGNAL(written()));
        QVERIFY(!b.masterOn());
        QVERIFY(b.setMasterOn(true));
        QVERIFY(b.masterOn());
        QVERIFY(b.setDebugLog(true));
        QVERIFY(b.setIntervalMinutes(720));
        QCOMPARE(b.intervalMinutes(), 720);
        QVERIFY(!b.setIntervalMinutes(61));                  // not in the list: refused
        QCOMPARE(b.intervalMinutes(), 720);
        QCOMPARE(written.count(), 3);
        settings::Settings other(sp);                        // another object sees what was written
        QVERIFY(other.masterOn());
        QVERIFY(other.debugLog());
        QCOMPARE(other.intervalMinutes(), 720);
    }

    void intervalChoicesAreTheAllowedList()
    {
        SettingsBridge b(fresh("ic") + ".conf", fresh("prefs") + ".conf");
        const QVariantList ch = b.intervalChoices();
        QCOMPARE(ch.size(), settings::allowedIntervalMinutes().size());
        QCOMPARE(ch.size(), 7);
        for (int i = 0; i < ch.size(); ++i)
            QCOMPARE(ch.at(i).toMap().value("minutes").toInt(), settings::allowedIntervalMinutes().at(i));
        QCOMPARE(ch.at(2).toMap().value("label").toString(), QString("24 hours"));
        QCOMPARE(b.intervalIndex(), 2);                      // the default, 24 h
        b.setIntervalMinutes(43200);
        QCOMPARE(b.intervalIndex(), 6);
    }

    void blacklistEdits()
    {
        SettingsBridge b(fresh("bl") + ".conf", fresh("prefs") + ".conf");
        QSignalSpy changed(&b, SIGNAL(blacklistChanged()));
        QVERIFY(b.addBlacklist("  banking "));
        QVERIFY(b.addBlacklist("news"));
        QVERIFY(!b.addBlacklist("   "));                     // blank
        QVERIFY(!b.addBlacklist(""));
        QVERIFY(!b.addBlacklist("NEWS"));                    // same without case
        QCOMPARE(b.blacklist(), QStringList() << "banking" << "news");
        QCOMPARE(changed.count(), 2);
        QVERIFY(b.removeBlacklist(0));
        QCOMPARE(b.blacklist(), QStringList() << "news");
        QVERIFY(!b.removeBlacklist(5));
        QVERIFY(!b.removeBlacklist(-1));
    }

    void notificationNoticeIsRemembered()
    {
        const QString pp = fresh("prefs") + ".conf";
        const QString sp = fresh("nn") + ".conf";
        {
            SettingsBridge b(sp, pp);
            QVERIFY(b.notificationNoticeNeeded());
            b.acceptNotificationNotice();
            QVERIFY(!b.notificationNoticeNeeded());
        }
        SettingsBridge again(sp, pp);
        QVERIFY(!again.notificationNoticeNeeded());
        // the flag is UI-private: the settings file the service reads has no such key
        QVERIFY(!readFile(sp).contains("notificationReadingAccepted"));
    }

    void settingsBridgeSeesExternalWrites()
    {
        const QString sp = fresh("ext") + ".conf";
        SettingsBridge b(sp, fresh("prefs") + ".conf");
        QSignalSpy master(&b, SIGNAL(masterOnChanged()));
        b.setDebugLog(true);                                 // the file exists
        appendToFile(sp, "notifications", "masterOn=true\nblacklist=x");
        b.reload();
        QVERIFY(b.masterOn());
        QCOMPARE(b.blacklist(), QStringList() << "x");
        QCOMPARE(master.count(), 1);
    }

    // ------------------------------------------------------------ apps model

    void appsAreSortedAndEdited()
    {
        const QString sp = fresh("apps") + ".conf";
        settings::Settings s(sp);
        s.discoverApp("Mail");
        s.discoverApp("calendar");
        s.discoverApp("Messages");
        s.setAppEnabled("Messages", true);
        AppsModel m(&s);
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(m.data(m.index(0), AppsModel::NameRole).toString(), QString("calendar"));
        QCOMPARE(m.data(m.index(1), AppsModel::NameRole).toString(), QString("Mail"));
        QCOMPARE(m.data(m.index(2), AppsModel::NameRole).toString(), QString("Messages"));
        QCOMPARE(m.data(m.index(2), AppsModel::EnabledRole).toBool(), true);
        QCOMPARE(m.data(m.index(0), AppsModel::EnabledRole).toBool(), false);   // new apps start off

        QSignalSpy written(&m, SIGNAL(written()));
        QSignalSpy data(&m, SIGNAL(dataChanged(QModelIndex, QModelIndex, QVector<int>)));
        QSignalSpy reset(&m, SIGNAL(modelReset()));
        QVERIFY(m.setEnabled(0, true));
        QCOMPARE(m.data(m.index(0), AppsModel::EnabledRole).toBool(), true);
        QCOMPARE(data.count(), 1);                           // a switch does not reset the list
        QCOMPARE(reset.count(), 0);
        QCOMPARE(written.count(), 1);
        QVERIFY(settings::Settings(sp).isAppEnabled("calendar"));

        QVERIFY(m.remove(1));                                // Mail
        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(m.count(), 2);
        QVERIFY(!settings::Settings(sp).apps().contains("Mail"));
        s.discoverApp("Mail");                               // back, disabled, with its next notification
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(m.data(m.index(1), AppsModel::EnabledRole).toBool(), false);
        QVERIFY(!m.setEnabled(9, true));
        QVERIFY(!m.remove(-1));
    }

    void blacklistedAppsAreNotListed()
    {
        settings::Settings s(fresh("bapps") + ".conf");
        s.discoverApp("Mail");
        s.discoverApp("Bank app");
        AppsModel m(&s);
        QCOMPARE(m.rowCount(), 2);
        s.setBlacklist(QStringList() << "bank");
        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(m.data(m.index(0), AppsModel::NameRole).toString(), QString("Mail"));
    }

    // --------------------------------------------------------- watches model

    void watchOrderMoveAndAlias()
    {
        const QString sp = fresh("w") + ".conf";
        Fixture f(sp, writeModels(), fresh("state") + ".json");
        f.settings.setWatches(QList<settings::WatchEntry>()
            << entry("AA:01", "CASIO DW-H5600", "dw-h5600", "Black watch", true)
            << entry("AA:02", "CASIO GW-B5600", "gw-b5600")
            << entry("AA:03", "CASIO GBD-100", "gbd-100", "Run"));
        WatchesModel &m = f.model;
        QCOMPARE(m.rowCount(), 3);
        QCOMPARE(m.data(m.index(0), WatchesModel::TitleRole).toString(), QString("Black watch"));
        QCOMPARE(m.data(m.index(1), WatchesModel::TitleRole).toString(), QString("CASIO GW-B5600")); // no alias
        QCOMPARE(m.data(m.index(0), WatchesModel::ModelLabelRole).toString(), QString("Casio DW-H5600"));
        QCOMPARE(m.data(m.index(0), WatchesModel::SupportStatusRole).toString(), QString("supported"));
        QCOMPARE(m.data(m.index(1), WatchesModel::SupportStatusRole).toString(), QString("untested"));

        QVERIFY(!m.canMoveUp(0));
        QVERIFY(m.canMoveUp(1));
        QVERIFY(m.canMoveDown(1));
        QVERIFY(!m.canMoveDown(2));
        QSignalSpy written(&m, SIGNAL(written()));
        QVERIFY(m.moveUp(2));
        QCOMPARE(m.data(m.index(1), WatchesModel::KeyRole).toString(), QString("AA:03"));
        QVERIFY(m.moveDown(0));
        QCOMPARE(m.data(m.index(0), WatchesModel::KeyRole).toString(), QString("AA:03"));
        QCOMPARE(m.data(m.index(1), WatchesModel::KeyRole).toString(), QString("AA:01"));
        QVERIFY(!m.moveUp(0));
        QVERIFY(!m.moveDown(2));
        QCOMPARE(written.count(), 2);
        // the new order is what the service will read (list order = session order)
        const QList<settings::WatchEntry> disk = settings::Settings(sp).watches();
        QCOMPARE(disk.at(0).key, QString("AA:03"));
        QCOMPARE(disk.at(1).key, QString("AA:01"));
        QCOMPARE(disk.at(2).key, QString("AA:02"));

        QVERIFY(m.setAlias("AA:02", "  Spare  "));          // alias edit, kept in the file
        QCOMPARE(m.data(m.index(2), WatchesModel::TitleRole).toString(), QString("Spare"));
        QCOMPARE(settings::Settings(sp).watches().at(2).alias, QString("Spare"));
        QVERIFY(m.setAlias("AA:02", ""));                    // back to the device name
        QCOMPARE(m.data(m.index(2), WatchesModel::TitleRole).toString(), QString("CASIO GW-B5600"));
        QVERIFY(!m.setAlias("ZZ:99", "x"));
    }

    void watchSilhouetteRuleAndPicker()
    {
        const QString sp = fresh("s") + ".conf";
        Fixture f(sp, writeModels(), fresh("state") + ".json");
        f.settings.setWatches(QList<settings::WatchEntry>()
            << entry("AA:01", "CASIO DW-H5600", "dw-h5600")                     // model default
            << entry("AA:02", "CASIO GW-B5600", "gw-b5600")                     // no default: classic
            << entry("AA:03", "CASIO GBD-100", "gbd-100")                       // default gbx-100
            << entry("AA:04", "CASIO ODD", "odd")                               // default unknown: classic
            << entry("AA:05", "CASIO DW-H5600", "dw-h5600", "", false, false, "round")   // own choice
            << entry("AA:06", "CASIO DW-H5600", "dw-h5600", "", false, false, "bogus")); // unknown own id
        WatchesModel &m = f.model;
        const char *expect[] = { "dw-h5600", "classic", "gbx-100", "classic", "round", "dw-h5600" };
        for (int i = 0; i < 6; ++i)
            QCOMPARE(m.data(m.index(i), WatchesModel::SilhouetteIdRole).toString(), QString(expect[i]));

        // the picker: only ids of the catalog; the choice is stored per watch
        QVERIFY(!m.setSilhouette("AA:02", "bogus"));
        QVERIFY(!m.setSilhouette("AA:02", ""));
        QVERIFY(m.setSilhouette("AA:02", "edifice"));
        QCOMPARE(m.data(m.index(1), WatchesModel::SilhouetteIdRole).toString(), QString("edifice"));
        QCOMPARE(settings::Settings(sp).watches().at(1).silhouette, QString("edifice"));
        QCOMPARE(settings::Settings(sp).watches().at(0).silhouette, QString());   // others untouched
        QVERIFY(m.setSilhouette("AA:01", "classic"));        // classic on a watch whose default is another
        QCOMPARE(m.data(m.index(0), WatchesModel::SilhouetteIdRole).toString(), QString("classic"));
    }

    void enableDisableAndRiskAcknowledgement()
    {
        const QString sp = fresh("e") + ".conf";
        Fixture f(sp, writeModels(), fresh("state") + ".json");
        f.settings.setWatches(QList<settings::WatchEntry>()
            << entry("AA:01", "CASIO DW-H5600", "dw-h5600")
            << entry("AA:02", "CASIO GW-B5600", "gw-b5600"));
        WatchesModel &m = f.model;
        QCOMPARE(m.enabledCount(), 0);
        QVERIFY(m.setEnabled("AA:01", true));
        QCOMPARE(m.enabledCount(), 1);
        QVERIFY(settings::Settings(sp).watches().at(0).enabled);
        QVERIFY(!settings::Settings(sp).watches().at(0).riskAcknowledged);
        // an untested model: the dialog's "Enable at my risk" stores the acknowledgement
        QVERIFY(m.enableAtOwnRisk("AA:02"));
        QVERIFY(settings::Settings(sp).watches().at(1).enabled);
        QVERIFY(settings::Settings(sp).watches().at(1).riskAcknowledged);
        QCOMPARE(m.data(m.index(1), WatchesModel::RiskAcknowledgedRole).toBool(), true);
        QVERIFY(m.setEnabled("AA:02", false));              // the acknowledgement stays
        QVERIFY(settings::Settings(sp).watches().at(1).riskAcknowledged);
        QVERIFY(!m.setEnabled("ZZ:99", true));
    }

    void forgetRemovesTheEntryOnly()
    {
        const QString sp = fresh("fg") + ".conf";
        Fixture f(sp, writeModels(), fresh("state") + ".json");
        f.settings.setWatches(QList<settings::WatchEntry>()
            << entry("AA:01", "CASIO DW-H5600", "dw-h5600", "One", true)
            << entry("AA:02", "CASIO GW-B5600", "gw-b5600", "Two"));
        WatchItem *item = f.model.watchItem("AA:02");
        QVERIFY(item->valid());
        QCOMPARE(item->title(), QString("Two"));
        QSignalSpy changed(item, SIGNAL(changed()));
        QVERIFY(f.model.forget("AA:02"));
        QCOMPARE(f.model.rowCount(), 1);
        QVERIFY(!item->valid());                             // the detail page can see it went
        QVERIFY(changed.count() >= 1);
        QVERIFY(!f.model.forget("AA:02"));
        QCOMPARE(settings::Settings(sp).watches().size(), 1);
    }

    void forgetHidesAListedDeviceThatIsNotInTheSettings()
    {
        const QString sp = fresh("fh") + ".conf";
        Fixture f(sp, writeModels(), fresh("state") + ".json");
        QVariantMap li;
        li.insert("key", "AA:09");
        li.insert("deviceName", "CASIO DW-H5600");
        li.insert("modelId", "dw-h5600");
        li.insert("foundNow", true);
        f.model.setListed(QList<QVariantMap>() << li);
        QCOMPARE(f.model.rowCount(), 1);
        QVERIFY(f.model.forget("AA:09"));
        QCOMPARE(f.model.rowCount(), 0);
        QVERIFY(settings::Settings(sp).hiddenDevices().contains("AA:09"));
        f.model.setListed(QList<QVariantMap>() << li);       // listed again: stays hidden
        QCOMPARE(f.model.rowCount(), 0);
        QVERIFY(!f.model.forget("AA:09"));
    }

    void liveStatusIsMergedByKey()
    {
        Fixture f(fresh("l") + ".conf", writeModels(), fresh("state") + ".json");
        f.settings.setWatches(QList<settings::WatchEntry>()
            << entry("AA:01", "CASIO DW-H5600", "dw-h5600", "One", true)
            << entry("AA:02", "CASIO GW-B5600", "gw-b5600", "Two", true));
        WatchesModel &m = f.model;
        QVERIFY(!m.data(m.index(0), WatchesModel::LiveRole).toBool());

        QVariantMap w1 = statusWatch("AA:01", true, 1000, 2000);
        w1.insert("running", true);
        w1.insert("lastError", "no watch in range");
        w1.insert("modelLabel", "Casio DW-H5600 (service)");
        w1.insert("lastOpKind", "timesync");
        w1.insert("lastOpTime", 1000);
        QVariantMap w2 = statusWatch("AA:02", false, 0, 0);
        w2.insert("problem", "untested model, not acknowledged");
        w2.insert("defaultSilhouette", "edifice");          // the service's value replaces the table
        m.setServiceStatus(QList<QVariantMap>() << w2 << w1, true);   // order of the answer does not matter

        QCOMPARE(m.data(m.index(0), WatchesModel::LiveRole).toBool(), true);
        QCOMPARE(m.data(m.index(0), WatchesModel::RunningRole).toBool(), true);
        QCOMPARE(m.data(m.index(0), WatchesModel::LastSuccessRole).toLongLong(), 1000LL);
        QCOMPARE(m.data(m.index(0), WatchesModel::NextSyncRole).toLongLong(), 2000LL);
        QCOMPARE(m.data(m.index(0), WatchesModel::LastErrorRole).toString(), QString("no watch in range"));
        QCOMPARE(m.data(m.index(0), WatchesModel::ModelLabelRole).toString(), QString("Casio DW-H5600 (service)"));
        QCOMPARE(m.data(m.index(0), WatchesModel::LastOpKindRole).toString(), QString("timesync"));
        QCOMPARE(m.data(m.index(1), WatchesModel::UsableRole).toBool(), false);
        QCOMPARE(m.data(m.index(1), WatchesModel::ProblemRole).toString(), QString("untested model, not acknowledged"));
        QCOMPARE(m.data(m.index(1), WatchesModel::SilhouetteIdRole).toString(), QString("edifice"));
        // the cover line: the oldest last success of the usable enabled watches
        QCOMPARE(m.oldestLastSuccess(), 1000LL);
        QVERIFY(m.anyUsable());
    }

    void withoutServiceTheRowsComeFromSettingsAndStateFile()
    {
        const QString st = fresh("state") + ".json";
        settings::State state(st);
        settings::WatchState ws;
        ws.lastSuccess = QDateTime::fromMSecsSinceEpoch(5000 * 1000, Qt::UTC);
        ws.lastError = "old error";
        ws.lastOpKind = "notif";
        ws.lastOpTime = QDateTime::fromMSecsSinceEpoch(6000 * 1000, Qt::UTC);
        state.setWatch("AA:01", ws);
        QVERIFY(state.save());
        Fixture f(fresh("n") + ".conf", writeModels(), st);
        f.settings.setWatches(QList<settings::WatchEntry>()
            << entry("AA:01", "CASIO DW-H5600", "dw-h5600", "One", true)
            << entry("AA:02", "CASIO GW-B5600", "gw-b5600", "Two", true)            // untested, no ack
            << entry("AA:03", "CASIO GW-B5600", "gw-b5600", "Three", true, true)    // acknowledged
            << entry("AA:04", "CASIO UNKNOWN", "", "Four", true));                  // not in the table
        f.model.setServiceStatus(QList<QVariantMap>(), false);
        WatchesModel &m = f.model;
        QCOMPARE(m.data(m.index(0), WatchesModel::LiveRole).toBool(), false);
        QCOMPARE(m.data(m.index(0), WatchesModel::LastSuccessRole).toLongLong(), 5000LL);
        QCOMPARE(m.data(m.index(0), WatchesModel::LastErrorRole).toString(), QString("old error"));
        QCOMPARE(m.data(m.index(0), WatchesModel::LastOpTimeRole).toLongLong(), 6000LL);
        // "usable" by the service's rule: supported, or acknowledged; unknown models never
        QCOMPARE(m.data(m.index(0), WatchesModel::UsableRole).toBool(), true);
        QCOMPARE(m.data(m.index(1), WatchesModel::UsableRole).toBool(), false);
        QCOMPARE(m.data(m.index(2), WatchesModel::UsableRole).toBool(), true);
        QCOMPARE(m.data(m.index(3), WatchesModel::UsableRole).toBool(), false);
        QCOMPARE(m.data(m.index(3), WatchesModel::SupportStatusRole).toString(), QString("unknown"));
        QVERIFY(m.anyUsable());
        QCOMPARE(m.oldestLastSuccess(), 0LL);                // a usable watch never synced
    }

    void pairedDevicesNotInSettingsAreListedAndCreated()
    {
        const QString sp = fresh("p") + ".conf";
        Fixture f(sp, writeModels(), fresh("state") + ".json");
        f.settings.setWatches(QList<settings::WatchEntry>()
            << entry("AA:01", "CASIO DW-H5600", "dw-h5600", "One"));
        QVariantMap a;                                       // in the settings, found now
        a.insert("key", "AA:01"); a.insert("foundNow", true); a.insert("inSettings", true);
        QVariantMap b;                                       // a new paired device
        b.insert("key", "BB:02"); b.insert("deviceName", "CASIO GW-B5600"); b.insert("bluezAlias", "x");
        b.insert("modelId", "gw-b5600"); b.insert("modelLabel", "Casio GW-B5600");
        b.insert("supportStatus", "untested"); b.insert("foundNow", true); b.insert("inSettings", false);
        f.model.setListed(QList<QVariantMap>() << a << b);
        WatchesModel &m = f.model;
        QCOMPARE(m.rowCount(), 2);
        QCOMPARE(m.data(m.index(0), WatchesModel::FoundRole).toInt(), 1);
        QCOMPARE(m.data(m.index(1), WatchesModel::KeyRole).toString(), QString("BB:02"));
        QCOMPARE(m.data(m.index(1), WatchesModel::InSettingsRole).toBool(), false);
        QCOMPARE(m.data(m.index(1), WatchesModel::TitleRole).toString(), QString("CASIO GW-B5600"));
        QCOMPARE(m.data(m.index(1), WatchesModel::EnabledRole).toBool(), false);
        QVERIFY(!m.canMoveDown(0));                          // a row outside the settings does not take part
        QVERIFY(!m.canMoveUp(1));

        QVariantMap gone;                                    // an entry that is not found now says so
        gone.insert("key", "AA:01"); gone.insert("foundNow", false); gone.insert("inSettings", true);
        f.model.setListed(QList<QVariantMap>() << gone);
        QCOMPARE(m.rowCount(), 1);
        QCOMPARE(m.data(m.index(0), WatchesModel::FoundRole).toInt(), 0);

        // switching on / acknowledging a listed device creates its entry (key = address)
        f.model.setListed(QList<QVariantMap>() << a << b);
        QVERIFY(m.enableAtOwnRisk("BB:02"));
        const QList<settings::WatchEntry> disk = settings::Settings(sp).watches();
        QCOMPARE(disk.size(), 2);
        QCOMPARE(disk.at(1).key, QString("BB:02"));
        QCOMPARE(disk.at(1).address, QString("BB:02"));
        QCOMPARE(disk.at(1).deviceName, QString("CASIO GW-B5600"));
        QCOMPARE(disk.at(1).modelId, QString("gw-b5600"));
        QVERIFY(disk.at(1).enabled);
        QVERIFY(disk.at(1).riskAcknowledged);
        QCOMPARE(disk.at(1).alias, QString());
        QCOMPARE(m.rowCount(), 2);                           // now an entry, not a second row
        QCOMPARE(m.data(m.index(1), WatchesModel::InSettingsRole).toBool(), true);
    }

    void enabledFilterFollowsTheSwitches()
    {
        Fixture f(fresh("ef") + ".conf", writeModels(), fresh("state") + ".json");
        f.settings.setWatches(QList<settings::WatchEntry>()
            << entry("AA:01", "CASIO DW-H5600", "dw-h5600", "One", true)
            << entry("AA:02", "CASIO DW-H5600", "dw-h5600", "Two", false)
            << entry("AA:03", "CASIO DW-H5600", "dw-h5600", "Three", true));
        EnabledWatchesModel e(&f.model);
        QCOMPARE(e.count(), 2);
        QCOMPARE(e.data(e.index(0, 0), WatchesModel::KeyRole).toString(), QString("AA:01"));
        QCOMPARE(e.data(e.index(1, 0), WatchesModel::KeyRole).toString(), QString("AA:03"));
        QSignalSpy count(&e, SIGNAL(countChanged()));
        f.model.setEnabled("AA:02", true);
        QCOMPARE(e.count(), 3);
        QCOMPARE(e.data(e.index(1, 0), WatchesModel::KeyRole).toString(), QString("AA:02")); // list order
        f.model.setEnabled("AA:01", false);
        QCOMPARE(e.count(), 2);
        QVERIFY(count.count() >= 2);
        f.model.moveDown(1);                                 // AA:02 and AA:03 swap
        QCOMPARE(e.data(e.index(0, 0), WatchesModel::KeyRole).toString(), QString("AA:03"));
    }

    // --------------------------------------------------- D-Bus client + peer

    void serviceAbsentIsAStateNotAnError()
    {
        const QString d = fresh("absent");
        QDir().mkpath(d);
        PrivateBus bus;
        if (!bus.start(d))
            QSKIP("dbus-daemon cannot be started here");
        QDBusConnection c = QDBusConnection::connectToBus(bus.address(), "tst-uibridge-absent");
        {
            ServiceClient client(c);
            QVERIFY(!client.available());
            QVERIFY(!client.statusKnown());
            QSignalSpy sync(&client, SIGNAL(syncRequested(bool)));
            QSignalSpy test(&client, SIGNAL(testNotificationResult(bool)));
            client.refresh();                                // no call, no crash
            client.listWatches();
            client.reloadSettings();
            client.syncAll();
            client.testNotification();
            QCOMPARE(sync.count(), 1);
            QCOMPARE(sync.at(0).at(0).toBool(), false);
            QCOMPARE(test.count(), 1);
            QCOMPARE(test.at(0).at(0).toBool(), false);
            QVERIFY(!client.listing());
        }
        QDBusConnection::disconnectFromBus("tst-uibridge-absent");
    }

    void clientTalksToTheServiceOnTheBus()
    {
        const QString d = fresh("peer");
        QDir().mkpath(d);
        PrivateBus bus;
        if (!bus.start(d))
            QSKIP("dbus-daemon cannot be started here");
        QDBusConnection server = QDBusConnection::connectToBus(bus.address(), "tst-uibridge-server");
        QDBusConnection cc = QDBusConnection::connectToBus(bus.address(), "tst-uibridge-client");

        FakeService svc;
        new FakeAdaptor(&svc);
        QVariantMap st;
        st.insert("serviceState", "running");
        st.insert("busy", false);
        st.insert("masterOn", true);
        st.insert("intervalMinutes", 1440);
        st.insert("modelTableLoaded", true);
        st.insert("monitorState", "running");
        st.insert("monitorReason", "");
        st.insert("riskProblems", QStringList());
        st.insert("watches", QVariant::fromValue(QList<QVariantMap>()
                    << statusWatch("AA:01", true, 1000, 2000) << statusWatch("AA:02", false, 0, 0)));
        svc.status = st;

        ServiceClient client(cc);                            // the service is not there yet
        QVERIFY(!client.available());
        QSignalSpy avail(&client, SIGNAL(availableChanged()));
        QSignalSpy status(&client, SIGNAL(statusChanged()));

        QVERIFY(server.registerObject(kServicePath, &svc));
        QVERIFY(server.registerService(kServiceName));
        QTRY_VERIFY_WITH_TIMEOUT(client.available(), 5000);   // it appears: the client notices
        QCOMPARE(avail.count(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(client.statusKnown(), 5000); // and asks for the status at once
        QCOMPARE(client.status().watches.size(), 2);          // nested aa{sv} through the bus
        QCOMPARE(client.status().watches.at(0).value("key").toString(), QString("AA:01"));
        QCOMPARE(client.status().watches.at(0).value("lastSuccess").toLongLong(), 1000LL);
        QCOMPARE(client.monitorState(), QString("running"));
        QVERIFY(!client.busy());

        QVariantMap st2 = st;                                 // StatusChanged: GetStatus again
        st2.insert("busy", true);
        svc.status = st2;
        status.clear();
        svc.changed();
        QTRY_VERIFY_WITH_TIMEOUT(client.busy(), 5000);
        QVERIFY(status.count() >= 1);

        QSignalSpy sync(&client, SIGNAL(syncRequested(bool)));   // one key, all keys, a refused key
        client.syncNow("AA:01");
        QTRY_COMPARE_WITH_TIMEOUT(sync.count(), 1, 5000);
        QCOMPARE(sync.at(0).at(0).toBool(), true);
        client.syncAll();
        QTRY_COMPARE_WITH_TIMEOUT(sync.count(), 2, 5000);
        client.syncNow("nope");
        QTRY_COMPARE_WITH_TIMEOUT(sync.count(), 3, 5000);
        QCOMPARE(sync.at(2).at(0).toBool(), false);
        QCOMPARE(svc.syncKeys, QStringList() << "AA:01" << "" << "nope");

        QSignalSpy test(&client, SIGNAL(testNotificationResult(bool)));
        client.testNotification();
        QTRY_COMPARE_WITH_TIMEOUT(test.count(), 1, 5000);
        QCOMPARE(test.at(0).at(0).toBool(), true);

        client.reloadSettings();
        QTRY_COMPARE_WITH_TIMEOUT(svc.reloadCalls, 1, 5000);

        QVariantMap w;                                        // ListWatches
        w.insert("key", "BB:02"); w.insert("deviceName", "CASIO GW-B5600");
        w.insert("foundNow", true); w.insert("inSettings", false);
        svc.watches = QList<QVariantMap>() << w;
        client.listWatches();
        QVERIFY(client.listing());
        QTRY_VERIFY_WITH_TIMEOUT(!client.listing(), 5000);
        QCOMPARE(client.listError(), QString());
        QCOMPARE(client.listedWatches().size(), 1);
        QCOMPARE(client.listedWatches().at(0).value("key").toString(), QString("BB:02"));

        svc.listFails = true;                                 // the Bluetooth error: a message, no crash
        client.listWatches();
        QTRY_VERIFY_WITH_TIMEOUT(!client.listing(), 5000);
        QVERIFY(client.listError().contains("BlueZ"));

        status.clear();                                       // the service goes away
        QVERIFY(server.unregisterService(kServiceName));
        QTRY_VERIFY_WITH_TIMEOUT(!client.available(), 5000);
        QVERIFY(!client.statusKnown());
        QVERIFY(status.count() >= 1);

        QDBusConnection::disconnectFromBus("tst-uibridge-client");
        QDBusConnection::disconnectFromBus("tst-uibridge-server");
    }

    // The whole bridge on a private bus: the service's status reaches the rows,
    // a write reaches ReloadSettings, the settings keep working without service.
    void bridgeWiresSettingsServiceAndModels()
    {
        const QString d = fresh("wire");
        QDir().mkpath(d);
        PrivateBus bus;
        if (!bus.start(d))
            QSKIP("dbus-daemon cannot be started here");
        QDBusConnection server = QDBusConnection::connectToBus(bus.address(), "tst-uibridge-wserver");
        QDBusConnection cc = QDBusConnection::connectToBus(bus.address(), "tst-uibridge-wclient");
        FakeService svc;
        new FakeAdaptor(&svc);
        QVERIFY(server.registerObject(kServicePath, &svc));
        QVERIFY(server.registerService(kServiceName));

        UiPaths p;
        p.settingsPath = fresh("wire") + ".conf";
        p.statePath = fresh("wirestate") + ".json";
        p.uiPrefsPath = fresh("wireprefs") + ".conf";
        p.modelsPath = writeModels();
        p.silhouettesDir = SILHOUETTES_DIR;
        {
            settings::Settings pre(p.settingsPath);
            pre.setWatches(QList<settings::WatchEntry>()
                << entry("AA:01", "CASIO DW-H5600", "dw-h5600", "One", true));
        }
        QVariantMap st;
        st.insert("serviceState", "running");
        st.insert("watches", QVariant::fromValue(QList<QVariantMap>() << statusWatch("AA:01", true, 1000, 2000)));
        svc.status = st;

        UiBridge bridge(p, cc);
        QVERIFY(bridge.service()->available());
        QTRY_VERIFY_WITH_TIMEOUT(bridge.service()->statusKnown(), 5000);
        QCOMPARE(bridge.watches()->data(bridge.watches()->index(0), WatchesModel::LastSuccessRole).toLongLong(), 1000LL);
        QCOMPARE(bridge.enabledWatches()->count(), 1);
        QVERIFY(bridge.lastSyncLine().startsWith("Last sync"));

        svc.reloadCalls = 0;                                  // a write is followed by ReloadSettings
        QVERIFY(bridge.settings()->setDebugLog(true));
        QTRY_COMPARE_WITH_TIMEOUT(svc.reloadCalls, 1, 5000);
        QVERIFY(bridge.watches()->setAlias("AA:01", "Renamed"));
        QTRY_COMPARE_WITH_TIMEOUT(svc.reloadCalls, 2, 5000);
        QCOMPARE(bridge.apps()->count(), 0);

        appendToFile(p.settingsPath, "notifications", "apps=Messages");   // the service discovered an app
        svc.changed();
        QTRY_COMPARE_WITH_TIMEOUT(bridge.apps()->count(), 1, 5000);

        QVERIFY(server.unregisterService(kServiceName));      // the service stops: rows stay, live data goes
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.service()->available(), 5000);
        QCOMPARE(bridge.watches()->rowCount(), 1);
        QCOMPARE(bridge.watches()->data(bridge.watches()->index(0), WatchesModel::LiveRole).toBool(), false);
        QCOMPARE(bridge.watches()->data(bridge.watches()->index(0), WatchesModel::TitleRole).toString(), QString("Renamed"));
        QVERIFY(bridge.settings()->setMasterOn(true));        // settings keep working

        QDBusConnection::disconnectFromBus("tst-uibridge-wclient");
        QDBusConnection::disconnectFromBus("tst-uibridge-wserver");
    }

    // -------------------------------------------------------- single instance

    void secondStartHandsOverAndExits()
    {
        const QString d = fresh("inst");
        QDir().mkpath(d);
        PrivateBus bus;
        if (!bus.start(d))
            QSKIP("dbus-daemon cannot be started here");
        QDBusConnection first = QDBusConnection::connectToBus(bus.address(), "tst-uibridge-first");
        InstanceGuard guard(first);
        QSignalSpy activate(&guard, SIGNAL(activateRequested()));
        QVERIFY(guard.claim(2000));                          // the first one runs

        for (int round = 0; round < 3; ++round) {            // three taps on the notification
            ClaimThread t;
            t.address = bus.address();
            t.start();
            QTRY_VERIFY_WITH_TIMEOUT(t.isFinished(), 10000);
            QVERIFY(!t.result);                              // each later start must exit
        }
        QCOMPARE(activate.count(), 3);                       // and the window comes forward each time
        QDBusConnection::disconnectFromBus("tst-uibridge-first");
    }

    // ------------------------------------------------------------------ text

    void firstSwitchOnNoticeText()
    {
        const QString js = readFile(QString(QML_DIR) + "/components/texts.js");
        const QString notice = "Casio Watch Sync will read all notifications on this phone to forward the ones you choose. "
                               "Nothing leaves the phone except the text of the notifications you enable, sent to your watch. Continue?";
        QVERIFY(js.contains("qsTr(\"" + notice + "\")"));
    }

    void aboutPageTextAndPlaceholder()
    {
        const QString js = readFile(QString(QML_DIR) + "/components/texts.js");
        QVERIFY(js.contains("Casio Watch Sync is an unofficial application."));

        // one function returns the project address (a placeholder), none typed in QML
        QVERIFY(!projectAddress().isEmpty());
        AppInfo info;
        QCOMPARE(info.address(), projectAddress());
        QCOMPARE(info.title(), QString("Casio Watch Sync"));
        QCOMPARE(info.license(), QString("GPL-3.0-or-later"));
        QVERIFY(!info.version().isEmpty());
        QDirIterator it(QML_DIR, QStringList() << "*.qml", QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            QVERIFY2(!readFile(it.next()).contains(projectAddress()), qPrintable(it.filePath()));
    }

    // Every user-visible string is in qsTr(); no QML file touches notification text.
    void qmlHasNoBareStringsAndNoNotificationText()
    {
        QDirIterator it(QML_DIR, QStringList() << "*.qml", QDir::Files, QDirIterator::Subdirectories);
        int files = 0;
        const QRegularExpression re("^(text|title|label|description|placeholderText|acceptText|cancelText)\\s*:\\s*\"");
        while (it.hasNext()) {
            const QString file = it.next();
            ++files;
            const QStringList lines = readFile(file).split('\n');
            for (int i = 0; i < lines.size(); ++i) {
                const QString l = lines.at(i).trimmed();
                if (l.startsWith("//"))
                    continue;
                const QString where = file + ":" + QString::number(i + 1) + ": " + l;
                QVERIFY2(!l.contains("notificationText"), qPrintable(where));
                QVERIFY2(!re.match(l).hasMatch(), qPrintable(where));
            }
        }
        QVERIFY(files >= 10);
    }
};

QTEST_GUILESS_MAIN(TstUiBridge)
#include "tst_uibridge.moc"
