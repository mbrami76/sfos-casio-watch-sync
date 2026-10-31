// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Tests for settings. Every test works in a temporary directory; nothing touches the user's
// real files.
#include "settings.h"

#include <QSignalSpy>
#include <QtTest>

using namespace settings;

namespace {

WatchEntry watch(const QString &key, const QString &alias = QString(), bool enabled = true)
{
    WatchEntry w;
    w.key = key;
    w.address = key;
    w.deviceName = QStringLiteral("CASIO DW-H5600");
    w.modelId = QStringLiteral("dw-h5600");
    w.alias = alias;
    w.enabled = enabled;
    return w;
}

void writeFile(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}

} // namespace

class TestSettings : public QObject
{
    Q_OBJECT
    QTemporaryDir dir;
    QString conf() const { return dir.path() + "/cfg/sfos-casio-sync.conf"; }
    QString statePath() const { return dir.path() + "/data/state.json"; }
private slots:
    void init()
    {
        QFile::remove(conf());
        QFile::remove(statePath());
        QDir(dir.path() + "/data").removeRecursively();
    }

    void defaultsOnMissingFile()
    {
        Settings s(conf());
        QCOMPARE(s.intervalMinutes(), 1440);
        QVERIFY(s.watches().isEmpty());
        QVERIFY(!s.masterOn());
        QVERIFY(s.blacklist().isEmpty());
        QVERIFY(!s.debugLog());
        QVERIFY(s.apps().isEmpty());
        QVERIFY(s.enabledApps().isEmpty());
    }
    void roundTripEverySetting()
    {
        {
            Settings s(conf());
            QVERIFY(s.setIntervalMinutes(360));
            QList<WatchEntry> l;
            WatchEntry a = watch("AA:BB:CC:DD:EE:01", QString::fromUtf8("Work \xC3\xA0"), true);
            a.riskAcknowledged = true;
            a.silhouette = "gbx-100";
            l << a << watch("AA:BB:CC:DD:EE:02", "Sport", false);
            QVERIFY(s.setWatches(l));
            QVERIFY(s.setMasterOn(true));
            QVERIFY(s.setBlacklist(QStringList() << "signal" << "Foo Bar"));
            QVERIFY(s.setDebugLog(true));
            QVERIFY(s.discoverApp("Messages"));
            QVERIFY(s.discoverApp("Calendar"));
            QVERIFY(s.discoverApp("Mail"));
            QVERIFY(s.setAppEnabled("Messages", true));
            QVERIFY(s.setAppEnabled("Mail", true));
            QVERIFY(s.removeApp("Mail"));
        }
        Settings t(conf());   // new object, same file
        QCOMPARE(t.intervalMinutes(), 360);
        QCOMPARE(t.watches().size(), 2);
        QCOMPARE(t.watches().at(0).alias, QString::fromUtf8("Work \xC3\xA0"));
        QVERIFY(t.watches().at(0).riskAcknowledged);
        QCOMPARE(t.watches().at(0).silhouette, QString("gbx-100"));
        QCOMPARE(t.watches().at(1).silhouette, QString());
        QVERIFY(t.watches().at(0).enabled);
        QVERIFY(!t.watches().at(1).enabled);
        QVERIFY(!t.watches().at(1).riskAcknowledged);
        QCOMPARE(t.watches().at(1).key, QString("AA:BB:CC:DD:EE:02"));
        QCOMPARE(t.watches().at(1).modelId, QString("dw-h5600"));
        QCOMPARE(t.watches().at(1).deviceName, QString("CASIO DW-H5600"));
        QVERIFY(t.masterOn());
        QCOMPARE(t.blacklist(), QStringList() << "signal" << "Foo Bar");
        QVERIFY(t.debugLog());
        QCOMPARE(t.apps(), QStringList() << "Messages" << "Calendar");
        QCOMPARE(t.enabledApps(), QStringList() << "Messages");
    }
    void silhouetteIsStoredAsGiven()
    {
        {
            Settings s(conf());
            QList<WatchEntry> l;
            WatchEntry a = watch("AA:01");                 // default: empty
            QVERIFY(a.silhouette.isEmpty());
            WatchEntry b = watch("AA:02");
            b.silhouette = QString::fromUtf8("no-such id \xC3\xA0");   // no validation of the id
            l << a << b;
            QVERIFY(s.setWatches(l));
        }
        Settings t(conf());
        QCOMPARE(t.watches().at(0).silhouette, QString());
        QCOMPARE(t.watches().at(1).silhouette, QString::fromUtf8("no-such id \xC3\xA0"));
        // a changed silhouette is a change of the entry
        QList<WatchEntry> l = t.watches();
        l[0].silhouette = "round";
        QVERIFY(t.setWatches(l));
        QCOMPARE(Settings(conf()).watches().at(0).silhouette, QString("round"));
    }
    void oldFileWithoutSilhouetteKey()
    {
        writeFile(conf(), "[watches]\n1\\key=AA:01\n1\\address=AA:01\n1\\deviceName=CASIO DW-H5600\n"
                          "1\\modelId=dw-h5600\n1\\alias=Old\n1\\enabled=true\n1\\riskAcknowledged=false\nsize=1\n");
        Settings s(conf());
        QCOMPARE(s.watches().size(), 1);
        QCOMPARE(s.watches().at(0).alias, QString("Old"));
        QCOMPARE(s.watches().at(0).silhouette, QString());
    }
    void intervalValidation()
    {
        Settings s(conf());
        QCOMPARE(allowedIntervalMinutes(),
                 QList<int>() << 360 << 720 << 1440 << 2880 << 10080 << 20160 << 43200);
        QVERIFY(allowedIntervalMinutes().contains(kDefaultIntervalMinutes));
        const QList<int> ok = allowedIntervalMinutes();
        for (int i = 0; i < ok.size(); ++i) {
            QVERIFY(s.setIntervalMinutes(ok.at(i)));
            QCOMPARE(s.intervalMinutes(), ok.at(i));
        }
        QCOMPARE(s.intervalMinutes(), 43200);
        const int bad[] = { 0, -5, 15, 359, 361, 1441, 60 * 24 * 7 + 1, 43201, 99999999 };
        for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            QVERIFY(!isAllowedInterval(bad[i]));
            QVERIFY(!s.setIntervalMinutes(bad[i]));
        }
        QCOMPARE(s.intervalMinutes(), 43200);               // invalid input changed nothing
    }
    void invalidStoredValuesFallBack()
    {
        writeFile(conf(), "[sync]\nintervalMinutes=5\n[notifications]\nmasterOn=maybe\n"
                          "blacklist=a, ,b\n");
        Settings s(conf());
        QCOMPARE(s.intervalMinutes(), 1440);
        QVERIFY(!s.masterOn());
        for (int i = 0; i < s.blacklist().size(); ++i)
            QVERIFY(!s.blacklist().at(i).isEmpty());
        writeFile(conf(), "[sync]\nintervalMinutes=abc\n");
        Settings t(conf());
        QCOMPARE(t.intervalMinutes(), 1440);
        writeFile(conf(), "[sync]\nintervalMinutes=99999999\n");
        Settings u(conf());
        QCOMPARE(u.intervalMinutes(), 1440);
        writeFile(conf(), "[sync]\nintervalMinutes=720\n");      // in the list
        QCOMPARE(Settings(conf()).intervalMinutes(), 720);
        writeFile(conf(), "[sync]\nintervalMinutes=1000\n");     // in the old range, not in the list
        QCOMPARE(Settings(conf()).intervalMinutes(), 1440);
    }
    void corruptIniGivesDefaults()
    {
        QByteArray junk("\xff\xfe\x00[[[=== not an ini\n\x01\x02 = = =\n[", 36);
        writeFile(conf(), junk);
        Settings s(conf());
        QCOMPARE(s.intervalMinutes(), 1440);
        QVERIFY(s.watches().isEmpty());
        QVERIFY(!s.masterOn());
        QVERIFY(s.apps().isEmpty());
        // and it stays usable
        QVERIFY(s.setIntervalMinutes(720));
        QCOMPARE(Settings(conf()).intervalMinutes(), 720);
    }
    void emptyFileGivesDefaults()
    {
        writeFile(conf(), "");
        Settings s(conf());
        QCOMPARE(s.intervalMinutes(), 1440);
    }
    void watchListValidationAndOrder()
    {
        Settings s(conf());
        QList<WatchEntry> l;
        l << watch("A") << watch("B") << watch("C");
        QVERIFY(s.setWatches(l));
        // duplicate key / empty key are refused and change nothing
        QList<WatchEntry> dup = l; dup << watch("A");
        QVERIFY(!s.setWatches(dup));
        QList<WatchEntry> empty = l; empty << watch("");
        QVERIFY(!s.setWatches(empty));
        QCOMPARE(s.watches().size(), 3);
        // reorder
        QVERIFY(s.moveWatch(2, 0));
        QCOMPARE(s.watches().at(0).key, QString("C"));
        QCOMPARE(s.watches().at(1).key, QString("A"));
        QCOMPARE(s.watches().at(2).key, QString("B"));
        QCOMPARE(Settings(conf()).watches().at(0).key, QString("C")); // persisted
        QVERIFY(s.moveWatch(0, 2));
        QCOMPARE(s.watches().at(2).key, QString("C"));
        QVERIFY(!s.moveWatch(3, 0));
        QVERIFY(!s.moveWatch(0, -1));
        // shrinking the list leaves no stale entries
        QList<WatchEntry> one; one << watch("Z");
        QVERIFY(s.setWatches(one));
        QCOMPARE(Settings(conf()).watches().size(), 1);
        QVERIFY(s.setWatches(QList<WatchEntry>()));
        QVERIFY(Settings(conf()).watches().isEmpty());
    }
    void duplicateKeysInFileAreSkipped()
    {
        writeFile(conf(), "[watches]\nsize=3\n1\\key=A\n1\\alias=first\n2\\key=A\n2\\alias=second\n"
                          "3\\key=\n3\\alias=none\n");
        Settings s(conf());
        QCOMPARE(s.watches().size(), 1);
        QCOMPARE(s.watches().at(0).alias, QString("first"));
    }
    void changeSignal()
    {
        Settings s(conf());
        QSignalSpy spy(&s, SIGNAL(changed()));
        QVERIFY(s.setIntervalMinutes(720));
        QCOMPARE(spy.count(), 1);
        QVERIFY(s.setIntervalMinutes(720));          // same value
        QCOMPARE(spy.count(), 1);
        QVERIFY(!s.setIntervalMinutes(1));           // invalid
        QCOMPARE(spy.count(), 1);
        s.setMasterOn(true); QCOMPARE(spy.count(), 2);
        s.setMasterOn(true); QCOMPARE(spy.count(), 2);
        s.setDebugLog(true); QCOMPARE(spy.count(), 3);
        s.setBlacklist(QStringList() << "x"); QCOMPARE(spy.count(), 4);
        s.setBlacklist(QStringList() << "x"); QCOMPARE(spy.count(), 4);
        s.setWatches(QList<WatchEntry>() << watch("A")); QCOMPARE(spy.count(), 5);
        s.discoverApp("App"); QCOMPARE(spy.count(), 6);
        s.discoverApp("App"); QCOMPARE(spy.count(), 6);
        s.setAppEnabled("App", true); QCOMPARE(spy.count(), 7);
        s.setAppEnabled("App", true); QCOMPARE(spy.count(), 7);
        s.removeApp("App"); QCOMPARE(spy.count(), 8);
        s.removeApp("App"); QCOMPARE(spy.count(), 8);
    }
    void reloadSeesExternalWrites()
    {
        Settings s(conf());
        s.setIntervalMinutes(720);
        QSignalSpy spy(&s, SIGNAL(changed()));
        s.reload();
        QCOMPARE(spy.count(), 0);                    // nothing changed
        // "the UI" is another process: write the file behind QSettings' back
        writeFile(conf(), "[sync]\nintervalMinutes=2880\n[notifications]\nmasterOn=true\n"
                          "debug=padding to change the size\n");
        s.reload();
        QCOMPARE(s.intervalMinutes(), 2880);
        QVERIFY(s.masterOn());
        QCOMPARE(spy.count(), 1);
    }
    void blacklistValidation()
    {
        Settings s(conf());
        QVERIFY(!s.setBlacklist(QStringList() << "ok" << ""));
        QVERIFY(!s.setBlacklist(QStringList() << "   "));
        QVERIFY(s.blacklist().isEmpty());
        QVERIFY(s.setBlacklist(QStringList() << "  padded  "));
        QCOMPARE(s.blacklist(), QStringList() << "padded");
        QVERIFY(s.setBlacklist(QStringList()));
        QVERIFY(s.blacklist().isEmpty());
    }

    // -------------------------------------------------------- app list
    void appListSemantics()
    {
        Settings s(conf());
        QVERIFY(s.discoverApp("Messages"));
        QVERIFY(!s.discoverApp("Messages"));             // already listed
        QVERIFY(!s.discoverApp(""));
        QVERIFY(!s.discoverApp("   "));
        QVERIFY(!s.isAppEnabled("Messages"));            // discovered apps are disabled
        QVERIFY(!s.setAppEnabled("Unknown", true));      // not listed
        QVERIFY(s.setAppEnabled("Messages", true));
        QVERIFY(s.isAppEnabled("Messages"));
        QVERIFY(s.setAppEnabled("Messages", false));
        QVERIFY(!s.isAppEnabled("Messages"));
        QVERIFY(s.setAppEnabled("Messages", true));
        // removal, then a new notification re-inserts it, disabled
        QVERIFY(s.removeApp("Messages"));
        QVERIFY(!s.removeApp("Messages"));
        QVERIFY(s.apps().isEmpty());
        QVERIFY(!s.isAppEnabled("Messages"));
        QVERIFY(s.discoverApp("Messages"));
        QCOMPARE(s.apps(), QStringList() << "Messages");
        QVERIFY(!s.isAppEnabled("Messages"));            // disabled again
    }
    void blacklistWinsOverEverything()
    {
        Settings s(conf());
        s.setBlacklist(QStringList() << "SIGNAL");
        QVERIFY(!s.discoverApp("Signal Private"));       // not inserted
        QVERIFY(s.apps().isEmpty());
        QVERIFY(s.discoverApp("Messages"));
        QVERIFY(s.setAppEnabled("Messages", true));
        QVERIFY(s.discoverApp("Telegram"));
        // blacklisting a listed (and enabled) app hides it everywhere
        s.setBlacklist(QStringList() << "messa");
        QCOMPARE(s.apps(), QStringList() << "Telegram");
        QVERIFY(s.enabledApps().isEmpty());
        QVERIFY(!s.isAppEnabled("Messages"));
        QVERIFY(!s.setAppEnabled("Messages", true));
        // lifting the blacklist brings it back as it was
        s.setBlacklist(QStringList());
        QCOMPARE(s.apps(), QStringList() << "Messages" << "Telegram");
        QVERIFY(s.isAppEnabled("Messages"));
    }
    void removedAppLeavesNoRecord()
    {
        // the old removedApps key is ignored; removal keeps nothing
        writeFile(conf(), "[notifications]\napps=A, B\nenabledApps=A\nremovedApps=C\n");
        Settings s(conf());
        QCOMPARE(s.apps(), QStringList() << "A" << "B");
        QVERIFY(s.removeApp("A"));
        s.reload();
        QCOMPARE(s.apps(), QStringList() << "B");
        QVERIFY(s.discoverApp("C"));                     // "removed" in the old file: just new
        QVERIFY(s.discoverApp("A"));
        QVERIFY(!s.isAppEnabled("A"));
        QVERIFY(!s.removeApp("Z"));
        QCOMPARE(s.apps(), QStringList() << "B" << "C" << "A");
    }
    void appNamesAreExact()
    {
        Settings s(conf());
        QVERIFY(s.discoverApp("App"));
        QVERIFY(s.discoverApp("app"));
        QCOMPARE(s.apps().size(), 2);
        QVERIFY(s.setAppEnabled("App", true));
        QVERIFY(!s.isAppEnabled("app"));
    }
    void appNamesWithSeparators()
    {
        Settings s(conf());
        QString odd = QString::fromUtf8("My, App/x=1 \xE2\x82\xAC");
        QVERIFY(s.discoverApp(odd));
        QVERIFY(s.setAppEnabled(odd, true));
        Settings t(conf());
        QCOMPARE(t.apps(), QStringList() << odd);
        QVERIFY(t.isAppEnabled(odd));
    }

    // ----------------------------------------------------------- state
    void stateMissingFile()
    {
        State st(statePath());
        QVERIFY(st.load());
        QVERIFY(st.watchKeys().isEmpty());
        QVERIFY(st.seenApps().isEmpty());
        QVERIFY(!st.hasWatch("A"));
        QVERIFY(st.watch("A").lastSuccess.isNull());
        QCOMPARE(st.watch("A").consecutiveFailures, 0);
    }
    void stateRoundTrip()
    {
        {
            State st(statePath());
            WatchState a;
            a.lastSuccess = QDateTime(QDate(2026, 10, 25), QTime(8, 30, 15), Qt::UTC);
            a.lastError = "connect timeout";
            a.lastAttempt = QDateTime(QDate(2026, 10, 25), QTime(9, 0, 0), Qt::UTC);
            a.consecutiveFailures = 2;
            a.lastOpTime = QDateTime(QDate(2026, 10, 25), QTime(8, 30, 20), Qt::UTC);
            a.lastOpKind = "timesync";
            a.bleAddress = "AA:BB:CC:DD:EE:FF";
            st.setWatch("A", a);
            st.setWatch("B", WatchState());
            st.addSeenApp("Messages");
            st.addSeenApp("Messages");
            st.addSeenApp("");
            st.addSeenApp("Calendar");
            QVERIFY(st.save());
        }
        State t(statePath());
        QVERIFY(t.load());
        QCOMPARE(t.watchKeys(), QStringList() << "A" << "B");
        WatchState a = t.watch("A");
        QCOMPARE(a.lastSuccess, QDateTime(QDate(2026, 10, 25), QTime(8, 30, 15), Qt::UTC));
        QCOMPARE(a.lastError, QString("connect timeout"));
        QCOMPARE(a.lastAttempt, QDateTime(QDate(2026, 10, 25), QTime(9, 0, 0), Qt::UTC));
        QCOMPARE(a.consecutiveFailures, 2);
        QCOMPARE(a.lastOpTime, QDateTime(QDate(2026, 10, 25), QTime(8, 30, 20), Qt::UTC));
        QCOMPARE(a.lastOpKind, QString("timesync"));
        QCOMPARE(a.bleAddress, QString("AA:BB:CC:DD:EE:FF"));
        QVERIFY(t.watch("B").lastSuccess.isNull());
        QVERIFY(t.watch("B") == WatchState());
        QCOMPARE(t.seenApps(), QStringList() << "Messages" << "Calendar");
    }
    void stateTimesAreStoredInUtc()
    {
        State st(statePath());
        WatchState a;
        // same instant given with an offset
        a.lastSuccess = QDateTime::fromString("2026-10-25T10:30:15+02:00", Qt::ISODate);
        st.setWatch("A", a);
        QVERIFY(st.save());
        State t(statePath());
        QVERIFY(t.load());
        QCOMPARE(t.watch("A").lastSuccess.toMSecsSinceEpoch(), a.lastSuccess.toMSecsSinceEpoch());
        QFile f(statePath());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QVERIFY(f.readAll().contains("2026-10-25T08:30:15Z"));
    }
    void stateCorruptFiles_data()
    {
        QTest::addColumn<QByteArray>("content");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("garbage") << QByteArray("\xff\xfe not json {{{");
        QTest::newRow("truncated") << QByteArray("{\"version\":1,\"watches\":{\"A\":{\"lastSucc");
        QTest::newRow("array") << QByteArray("[1,2,3]");
        QTest::newRow("scalar") << QByteArray("42");
    }
    void stateCorruptFiles()
    {
        QFETCH(QByteArray, content);
        writeFile(statePath(), content);
        State st(statePath());
        QVERIFY(!st.load());                       // reported, not a crash
        QVERIFY(st.watchKeys().isEmpty());
        QVERIFY(st.seenApps().isEmpty());
        // and it can be written over
        st.setWatch("A", WatchState());
        QVERIFY(st.save());
        State t(statePath());
        QVERIFY(t.load());
        QVERIFY(t.hasWatch("A"));
    }
    void stateBadFieldsAreIgnored()
    {
        writeFile(statePath(),
                  "{\"version\":1,\"watches\":{"
                  "\"A\":{\"lastSuccess\":\"not a date\",\"lastError\":5,\"consecutiveFailures\":-3,"
                  "\"lastOp\":\"x\",\"bleAddress\":null},"
                  "\"B\":\"nope\",\"\":{}},"
                  "\"seenApps\":[\"ok\",3,null,\"ok\"]}");
        State st(statePath());
        QVERIFY(st.load());
        QCOMPARE(st.watchKeys(), QStringList() << "A");
        QVERIFY(st.watch("A").lastSuccess.isNull());
        QCOMPARE(st.watch("A").consecutiveFailures, 0);
        QVERIFY(st.watch("A").lastError.isEmpty());
        QCOMPARE(st.seenApps(), QStringList() << "ok");
    }
    void stateAtomicWrite()
    {
        State st(statePath());
        st.setWatch("A", WatchState());
        QVERIFY(st.save());
        QByteArray first;
        { QFile f(statePath()); QVERIFY(f.open(QIODevice::ReadOnly)); first = f.readAll(); }
        WatchState w; w.lastError = "later";
        st.setWatch("A", w);
        QVERIFY(st.save());
        // only the final file is left in the directory (no temporary file)
        QDir d(QFileInfo(statePath()).absolutePath());
        QCOMPARE(d.entryList(QDir::Files | QDir::Hidden).size(), 1);
        // the file always parses completely
        QFile f(statePath()); QVERIFY(f.open(QIODevice::ReadOnly));
        QByteArray second = f.readAll();
        QVERIFY(second != first);
        QJsonParseError pe;
        QJsonDocument::fromJson(second, &pe);
        QCOMPARE(pe.error, QJsonParseError::NoError);
        // a failing save leaves the old file untouched: the target is a
        // directory path that cannot be created below a regular file
        State bad(statePath() + "/sub/state.json");
        QVERIFY(!bad.save());
        QFile again(statePath()); QVERIFY(again.open(QIODevice::ReadOnly));
        QCOMPARE(again.readAll(), second);
    }
    void stateRemoveAndClear()
    {
        State st(statePath());
        st.setWatch("A", WatchState()); st.setWatch("B", WatchState());
        st.removeWatch("A");
        QCOMPARE(st.watchKeys(), QStringList() << "B");
        st.setWatch("", WatchState());                 // empty key ignored
        QCOMPARE(st.watchKeys().size(), 1);
        st.addSeenApp("x"); st.clearSeenApps();
        QVERIFY(st.seenApps().isEmpty());
    }

    // ---------------------------------------------------------- models
    void realModelsFileLoads()
    {
        watchproto::ModelTable t;
        QString err;
        QVERIFY2(loadModelTable(&t, QString(), MODELS_JSON, &err), qPrintable(err));
        const watchproto::ModelInfo *m = t.find("CASIO DW-H5600");
        QVERIFY(m != 0);
        QCOMPARE(m->id, QString("dw-h5600"));
        QCOMPARE(int(m->status), int(watchproto::ModelSupported));
        QCOMPARE(int(m->dowEncoding), int(watchproto::DowSun0));
        QCOMPARE(m->defaultSilhouette, QString("dw-h5600"));
        QVERIFY(t.find("CASIO SOMETHING-ELSE") == 0);
    }
    void installedPathWinsThenFallback()
    {
        QString good = dir.path() + "/good.json", other = dir.path() + "/other.json";
        writeFile(good, "{\"version\":1,\"models\":[{\"id\":\"a\",\"label\":\"A\",\"family\":\"f\","
                        "\"status\":\"untested\",\"namePatterns\":[\"A\"]}]}");
        watchproto::ModelTable t;
        QString err;
        // installed path missing -> fallback used
        QVERIFY(loadModelTable(&t, dir.path() + "/missing.json", good, &err));
        QVERIFY(t.find("A") != 0);
        // installed path valid -> fallback not consulted
        writeFile(other, "{\"version\":1,\"models\":[{\"id\":\"b\",\"label\":\"B\",\"family\":\"f\","
                         "\"status\":\"untested\",\"namePatterns\":[\"B\"]}]}");
        QVERIFY(loadModelTable(&t, other, good, &err));
        QVERIFY(t.find("B") != 0 && t.find("A") == 0);
        // installed path corrupt -> fallback
        QString bad = dir.path() + "/bad.json";
        writeFile(bad, "{ nope");
        QVERIFY(loadModelTable(&t, bad, good, &err));
        QVERIFY(t.find("A") != 0);
    }
    void noUsableModelsFile()
    {
        watchproto::ModelTable t;
        QString err;
        QVERIFY(!loadModelTable(&t, dir.path() + "/missing.json", QString(), &err));
        QVERIFY(!err.isEmpty());
        QCOMPARE(t.count(), 0);
        QString bad = dir.path() + "/bad.json";
        writeFile(bad, "{ nope");
        QVERIFY(!loadModelTable(&t, bad, QString(), &err));
        QCOMPARE(t.count(), 0);
        QVERIFY(!loadModelTable(&t, QString(), QString(), &err));
        // a previously loaded table is emptied by a failed load
        QString good = dir.path() + "/good.json";
        writeFile(good, "{\"version\":1,\"models\":[{\"id\":\"a\",\"label\":\"A\",\"family\":\"f\","
                        "\"status\":\"untested\",\"namePatterns\":[\"A\"]}]}");
        QVERIFY(loadModelTable(&t, good));
        QVERIFY(!loadModelTable(&t, bad));
        QCOMPARE(t.count(), 0);
    }
    void defaultPaths()
    {
        QVERIFY(defaultSettingsPath().endsWith("/it.michelebrami/sfos-casio-sync.conf"));
        QVERIFY(defaultStatePath().endsWith("/it.michelebrami/sfos-casio-sync/state.json"));
        QCOMPARE(installedModelsPath(), QString("/usr/share/sfos-casio-sync/models.json"));
    }
};

QTEST_MAIN(TestSettings)

#include "tst_settings.moc"
