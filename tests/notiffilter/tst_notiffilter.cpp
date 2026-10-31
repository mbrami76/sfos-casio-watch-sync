// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Tests for notiffilter.
#include "notiffilter.h"

#include <QtTest>

using namespace notiffilter;

namespace {

IncomingNotification incoming(const QString &app, const QString &summary, const QString &body)
{
    IncomingNotification n;
    n.appName = app;
    n.summary = summary;
    n.body = body;
    return n;
}

NotifSettings settings()
{
    NotifSettings s;
    s.masterOn = true;
    s.enabledApps << QStringLiteral("Messages") << QStringLiteral("Calendar");
    s.categoryByApp = defaultCategoryTable();
    s.ownAppName = QStringLiteral("Casio Watch Sync");
    return s;
}

std::string str(const char *s) { return std::string(s); }

} // namespace

class TestNotifFilter : public QObject
{
    Q_OBJECT
private slots:
    // ------------------------------------------------------------ decisions
    void forwardsEnabledApp()
    {
        FilterResult r = filter(incoming("Messages", "Alex", "Hiya"), settings());
        QCOMPARE(r.decision, Forward);
        QVERIFY(r.forward());
        QVERIFY(!r.discoverApp);
        QVERIFY(r.n.alert);
        QCOMPARE(r.n.sender, str("Messages"));
        QCOMPARE(r.n.title, str("Alex"));
        QVERIFY(r.n.subtitle.empty());
        QCOMPARE(r.n.message, str("Hiya"));
        QCOMPARE(int(r.n.category), 0);                  // no app name shipped
    }
    void masterOffDropsEverything()
    {
        NotifSettings s = settings();
        s.masterOn = false;
        FilterResult r = filter(incoming("Messages", "a", "b"), s);
        QCOMPARE(r.decision, DropMasterOff);
        QVERIFY(!r.discoverApp);
        // also for an app that would be discovered
        r = filter(incoming("NewApp", "a", "b"), s);
        QCOMPARE(r.decision, DropMasterOff);
        QVERIFY(!r.discoverApp);
    }
    void ownAppIsDroppedEvenIfEnabled()
    {
        NotifSettings s = settings();
        s.enabledApps << s.ownAppName;
        FilterResult r = filter(incoming("Casio Watch Sync", "time sync", "x"), s);
        QCOMPARE(r.decision, DropOwnApp);
        QVERIFY(!r.discoverApp);
        r = filter(incoming("casio watch sync", "t", "x"), s);
        QCOMPARE(r.decision, DropOwnApp);
        // own app beats the blacklist and the private hint in the reported reason
        s.blacklist << "casio";
        r = filter(incoming("Casio Watch Sync", "t", "x"), s);
        QCOMPARE(r.decision, DropOwnApp);
    }
    void emptyOwnNameDisablesTheRule()
    {
        NotifSettings s = settings();
        s.ownAppName.clear();
        s.enabledApps << QStringLiteral("");
        FilterResult r = filter(incoming("", "t", "b"), s);
        QCOMPARE(r.decision, Forward); // empty app name enabled explicitly: not the own app
    }
    void blacklistBeatsEnabledAndIsNotDiscovered()
    {
        NotifSettings s = settings();
        s.blacklist << "messa";
        FilterResult r = filter(incoming("Messages", "a", "b"), s);
        QCOMPARE(r.decision, DropBlacklisted);
        QVERIFY(!r.discoverApp);
        r = filter(incoming("Secret Messaging", "a", "b"), s); // not enabled either
        QCOMPARE(r.decision, DropBlacklisted);
        QVERIFY(!r.discoverApp);
    }
    void privateVisibilityIsDropped()
    {
        IncomingNotification n = incoming("Messages", "a", "b");
        n.hints.insert("x-nemo-visibility", "private");
        FilterResult r = filter(n, settings());
        QCOMPARE(r.decision, DropPrivate);
        QVERIFY(!r.discoverApp);
        n.hints.insert("x-nemo-visibility", " PRIVATE ");
        QCOMPARE(filter(n, settings()).decision, DropPrivate);
        // a private notification of a not-enabled app is not discovered
        n.appName = "NewApp";
        r = filter(n, settings());
        QCOMPARE(r.decision, DropPrivate);
        QVERIFY(!r.discoverApp);
    }
    void otherVisibilityAndMissingHintsAllow()
    {
        IncomingNotification n = incoming("Messages", "a", "b");
        QCOMPARE(filter(n, settings()).decision, Forward);   // SMS has no hint
        n.hints.insert("x-nemo-visibility", "public");
        QCOMPARE(filter(n, settings()).decision, Forward);
        n.hints.insert("x-nemo-visibility", QVariant());
        QCOMPARE(filter(n, settings()).decision, Forward);
        n.hints.insert("x-nemo-visibility", 42);
        QCOMPARE(filter(n, settings()).decision, Forward);
    }
    void emptyTextIsDropped()
    {
        FilterResult r = filter(incoming("Messages", "", ""), settings());
        QCOMPARE(r.decision, DropEmpty);
        QVERIFY(!r.discoverApp);
        QCOMPARE(filter(incoming("Messages", "  \n", "\t "), settings()).decision, DropEmpty);
        // empty text of a not-enabled app: dropped as empty, not discovered
        r = filter(incoming("NewApp", "", ""), settings());
        QCOMPARE(r.decision, DropEmpty);
        QVERIFY(!r.discoverApp);
    }
    void notEnabledIsDroppedAndDiscovered()
    {
        FilterResult r = filter(incoming("NewApp", "a", "b"), settings());
        QCOMPARE(r.decision, DropNotEnabled);
        QVERIFY(r.discoverApp);
        // the enabled set is exact: another case is another app
        r = filter(incoming("messages", "a", "b"), settings());
        QCOMPARE(r.decision, DropNotEnabled);
        QVERIFY(r.discoverApp);
    }
    void emptyAppNameIsNotDiscovered()
    {
        FilterResult r = filter(incoming("", "a", "b"), settings());
        QCOMPARE(r.decision, DropNotEnabled);
        QVERIFY(!r.discoverApp);
        r = filter(incoming("   ", "a", "b"), settings());
        QVERIFY(!r.discoverApp);
    }
    void bodyOnlyAndSummaryOnly()
    {
        FilterResult r = filter(incoming("Messages", "", "only body"), settings());
        QCOMPARE(r.decision, Forward);
        QVERIFY(r.n.title.empty());
        QCOMPARE(r.n.message, str("only body"));
        r = filter(incoming("Messages", "only summary", ""), settings());
        QCOMPARE(r.decision, Forward);
        QCOMPARE(r.n.title, str("only summary"));
        QVERIFY(r.n.message.empty());
    }
    void textPassesUnchangedAsUtf8()
    {
        // normalisation and truncation belong to watchproto, not here
        FilterResult r = filter(incoming("Messages", QString::fromUtf8("T\xC3\xA0\r\nx"),
                                         QString::fromUtf8("\xE2\x82\xAC\t1\n2")), settings());
        QCOMPARE(r.n.title, str("T\xC3\xA0\r\nx"));
        QCOMPARE(r.n.message, str("\xE2\x82\xAC\t1\n2"));
    }
    void noStateBetweenCalls()
    {
        NotifSettings s = settings();
        IncomingNotification a = incoming("NewApp", "a", "b");
        FilterResult r1 = filter(a, s), r2 = filter(a, s);
        QCOMPARE(r1.decision, r2.decision);
        QVERIFY(r1.discoverApp && r2.discoverApp);       // still discovered the second time
        QVERIFY(!s.enabledApps.contains("NewApp"));      // settings untouched
        QCOMPARE(filter(incoming("Messages", "a", "b"), s).decision, Forward);
        QCOMPARE(filter(a, s).decision, DropNotEnabled);
    }
    void whenAndReplacesIdAreIgnored()
    {
        IncomingNotification n = incoming("Messages", "a", "b");
        n.replacesId = 7;
        n.when = QDateTime(QDate(2026, 10, 25), QTime(1, 2, 3));
        FilterResult r = filter(n, settings());
        QCOMPARE(r.decision, Forward);
    }
    void forwardedIsAcceptedByWatchproto()
    {
        FilterResult r = filter(incoming("Calendar", "Meeting", "10:00"), settings());
        QCOMPARE(int(r.n.category), 0);                  // unlisted app: generic
        watchproto::Notification w;
        w.id = 1;
        w.alert = r.n.alert;
        w.category = r.n.category;
        w.timestamp = watchproto::DateTime(2026, 10, 25, 10, 0, 0);
        w.sender = r.n.sender; w.title = r.n.title; w.subtitle = r.n.subtitle; w.message = r.n.message;
        QVERIFY(watchproto::buildNotificationFrame(watchproto::fitToWatch(w)).ok());
    }

    // ------------------------------------------------------------ blacklist
    void blacklist_data()
    {
        QTest::addColumn<QString>("app");
        QTest::addColumn<QStringList>("list");
        QTest::addColumn<bool>("expected");
        QTest::newRow("empty list") << "Signal" << QStringList() << false;
        QTest::newRow("exact") << "Signal" << (QStringList() << "Signal") << true;
        QTest::newRow("substring") << "Signal Private Messenger" << (QStringList() << "private") << true;
        QTest::newRow("case") << "SIGNAL" << (QStringList() << "signal") << true;
        QTest::newRow("accents same case") << QString::fromUtf8("Caf\xC3\xA9") << (QStringList() << QString::fromUtf8("f\xC3\xA9")) << true;
        QTest::newRow("accents upper") << QString::fromUtf8("CAF\xC3\x89") << (QStringList() << QString::fromUtf8("caf\xC3\xA9")) << true;
        QTest::newRow("accent is not folded") << "Cafe" << (QStringList() << QString::fromUtf8("caf\xC3\xA9")) << false;
        QTest::newRow("no match") << "Signal" << (QStringList() << "Telegram") << false;
        QTest::newRow("empty entry ignored") << "Signal" << (QStringList() << "") << false;
        QTest::newRow("blank entry ignored") << "Signal" << (QStringList() << "  ") << false;
        QTest::newRow("empty then real") << "Signal" << (QStringList() << "" << "gnal") << true;
        QTest::newRow("entry trimmed") << "Signal" << (QStringList() << " gnal ") << true;
        QTest::newRow("inner space kept") << "My App" << (QStringList() << "y a") << true;
        QTest::newRow("star is literal") << "Signal" << (QStringList() << "Sig*") << false;
        QTest::newRow("star literal match") << "Sig*" << (QStringList() << "g*") << true;
        QTest::newRow("regex is literal") << "Signal" << (QStringList() << "S.gnal") << false;
        QTest::newRow("regex literal match") << "S.gnal" << (QStringList() << "S.g") << true;
        QTest::newRow("anchors literal") << "Signal" << (QStringList() << "^Signal$") << false;
        QTest::newRow("entry longer than app") << "Sig" << (QStringList() << "Signal") << false;
        QTest::newRow("empty app") << "" << (QStringList() << "x") << false;
    }
    void blacklist()
    {
        QFETCH(QString, app);
        QFETCH(QStringList, list);
        QFETCH(bool, expected);
        QCOMPARE(isBlacklisted(app, list), expected);
    }

    // ----------------------------------------------------------- categories
    void defaultTable()
    {
        QMap<QString, int> t = defaultCategoryTable();
        QVERIFY(t.isEmpty());                        // no app name shipped
        QCOMPARE(categoryFor("Messages", t), 0);
        QCOMPARE(categoryFor("Calendar", t), 0);
        QCOMPARE(categoryFor("E-mail", t), 0);
        QCOMPARE(categoryFor("Unknown App", t), 0);
        QCOMPARE(categoryFor("", t), 0);
        // never the call or voicemail icons (1-3), only verified categories
        for (QMap<QString, int>::const_iterator it = t.constBegin(); it != t.constEnd(); ++it)
            QVERIFY(it.value() == 4 || it.value() == 5 || it.value() == 6);
    }
    void categoryLookupAndDefault()
    {
        QMap<QString, int> t;
        t.insert("Foo", 5);
        t.insert("Bad", 99);
        t.insert("Neg", -1);
        QCOMPARE(categoryFor("foo", t), 5);
        QCOMPARE(categoryFor("FOO", t), 5);
        QCOMPARE(categoryFor("Bad", t), 0);
        QCOMPARE(categoryFor("Neg", t), 0);
        QCOMPARE(categoryFor("Bar", t), 0);
        NotifSettings s = settings();
        s.categoryByApp = t;
        s.enabledApps << "Foo" << "Bar";
        QCOMPARE(int(filter(incoming("Foo", "a", "b"), s).n.category), 5);
        QCOMPARE(int(filter(incoming("Bar", "a", "b"), s).n.category), 0);
        s.categoryByApp.clear();
        QCOMPARE(int(filter(incoming("Foo", "a", "b"), s).n.category), 0);
    }

    // --------------------------------------------------------------- sender
    void senderShortening()
    {
        QCOMPARE(shortenSender("Messages"), str("Messages"));
        QCOMPARE(shortenSender(""), str(""));
        QCOMPARE(shortenSender("  Foo \n\t Bar  "), str("Foo Bar"));
        QCOMPARE(shortenSender("12345678901234567890"), str("12345678901234567890")); // 20
        QCOMPARE(shortenSender("123456789012345678901"), str("12345678901234567890"));
        // 2-byte characters: 20 characters, 40 bytes
        QString two = QString(30, QChar(0x00E0));
        std::string r = shortenSender(two);
        QCOMPARE(r.size(), size_t(40));
        QCOMPARE(watchproto::countCodePoints(r), size_t(20));
        QVERIFY(watchproto::isValidUtf8(r));
        // 3-byte characters: the byte limit cuts first (13 x 3 = 39 bytes)
        r = shortenSender(QString(30, QChar(0x20AC)));
        QCOMPARE(r.size(), size_t(39));
        QVERIFY(watchproto::isValidUtf8(r));
        // 4-byte characters (surrogate pairs): 10 x 4 = 40 bytes
        QString emoji;
        for (int i = 0; i < 15; ++i) { emoji += QChar(0xD83D); emoji += QChar(0xDE00); }
        r = shortenSender(emoji);
        QCOMPARE(r.size(), size_t(40));
        QVERIFY(watchproto::isValidUtf8(r));
        // lone surrogate becomes a replacement, still valid UTF-8
        QVERIFY(watchproto::isValidUtf8(shortenSender(QString(3, QChar(0xD83D)))));
    }
    void longSenderIsShortInTheResult()
    {
        NotifSettings s = settings();
        QString name = QString(60, QChar('x'));
        s.enabledApps << name;
        FilterResult r = filter(incoming(name, "t", "m"), s);
        QCOMPARE(r.n.sender.size(), size_t(20));
    }
};

QTEST_APPLESS_MAIN(TestNotifFilter)

#include "tst_notiffilter.moc"
