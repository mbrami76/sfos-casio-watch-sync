// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Tests for watchproto and the model table. Golden vectors were captured from the watch.
#include "modeltable.h"
#include "watchproto.h"

#include <QtTest>

using namespace watchproto;

namespace {

Bytes hex(const char *s)
{
    QByteArray raw = QByteArray::fromHex(s);
    return Bytes(raw.constData(), raw.constData() + raw.size());
}

std::string str(const char *s) { return std::string(s); }

QByteArray toQ(const Bytes &b)
{
    return QByteArray(reinterpret_cast<const char *>(b.data()), int(b.size()));
}

std::string repeat(const std::string &unit, int n)
{
    std::string r;
    for (int i = 0; i < n; ++i)
        r += unit;
    return r;
}

const DateTime kSat(2026, 10, 24, 16, 4, 33);

// Frame builder helper: a notification with a valid header.
Notification makeNotif()
{
    Notification n;
    n.id = 9004;
    n.category = kCategoryCalendar;
    n.timestamp = DateTime(2026, 10, 24, 17, 3, 32);
    return n;
}

} // namespace

class TestTimeFrame : public QObject
{
    Q_OBJECT
private slots:
    void golden_data()
    {
        QTest::addColumn<QByteArray>("frame");
        QTest::addColumn<int>("y"); QTest::addColumn<int>("mo");
        QTest::addColumn<int>("d"); QTest::addColumn<int>("h");
        QTest::addColumn<int>("mi"); QTest::addColumn<int>("s");
        QTest::addColumn<int>("dow");
        // tests 1, 2, 4, 5
        QTest::newRow("sat real")  << QByteArray::fromHex("09ea070a18100421060001") << 2026 << 10 << 24 << 16 << 4 << 33 << 6;
        QTest::newRow("sat +1h")   << QByteArray::fromHex("09ea070a18110530060001") << 2026 << 10 << 24 << 17 << 5 << 48 << 6;
        QTest::newRow("mon")       << QByteArray::fromHex("09ea070a1a10080601" "0001") << 2026 << 10 << 26 << 16 << 8 << 6 << 1;
        QTest::newRow("sat 16:08") << QByteArray::fromHex("09ea070a181008" "1e060001") << 2026 << 10 << 24 << 16 << 8 << 30 << 6;
    }
    void golden()
    {
        QFETCH(QByteArray, frame); QFETCH(int, y); QFETCH(int, mo);
        QFETCH(int, d); QFETCH(int, h); QFETCH(int, mi); QFETCH(int, s);
        QFETCH(int, dow);
        TimeFrame tf = buildTimeFrame(DateTime(y, mo, d, h, mi, s), DowSun0);
        QVERIFY(tf.ok());
        QCOMPARE(toQ(tf.bytes), frame);
        QCOMPARE(tf.bytes.size(), kTimeFrameSize);

        DateTime dt; int rawDow = -1;
        QVERIFY(decodeTimeFrame(tf.bytes, &dt, &rawDow));
        QCOMPARE(dt.year, y); QCOMPARE(dt.month, mo); QCOMPARE(dt.day, d);
        QCOMPARE(dt.hour, h); QCOMPARE(dt.minute, mi); QCOMPARE(dt.second, s);
        QCOMPARE(rawDow, dow);
        QCOMPARE(toQ(buildTimeFrame(dt, DowSun0).bytes), frame); // re-encode
    }

    void dayOfWeekEncodings()
    {
        // Sunday 2026-10-25 (R-TIME-7 test day), Monday 2026-10-26, Saturday 24.
        QCOMPARE(dayOfWeek(2026, 10, 25, DowSun0), 0);
        QCOMPARE(dayOfWeek(2026, 10, 25, DowMon1), 7);
        QCOMPARE(dayOfWeek(2026, 10, 26, DowSun0), 1);
        QCOMPARE(dayOfWeek(2026, 10, 26, DowMon1), 1);
        QCOMPARE(dayOfWeek(2026, 10, 24, DowSun0), 6);
        QCOMPARE(dayOfWeek(2026, 10, 24, DowMon1), 6);
        QCOMPARE(dayOfWeek(2000, 1, 1, DowSun0), 6);  // Saturday
        QCOMPARE(dayOfWeek(2028, 2, 29, DowSun0), 2);  // Tuesday
        QCOMPARE(dayOfWeek(2099, 12, 31, DowSun0), 4); // Thursday
    }
    void dowByteFollowsEncoding()
    {
        Bytes sun0 = buildTimeFrame(DateTime(2026, 10, 25, 12, 0, 0), DowSun0).bytes;
        Bytes mon1 = buildTimeFrame(DateTime(2026, 10, 25, 12, 0, 0), DowMon1).bytes;
        QCOMPARE(int(sun0[8]), 0);
        QCOMPARE(int(mon1[8]), 7);
    }

    void invalid_data()
    {
        QTest::addColumn<int>("y"); QTest::addColumn<int>("mo");
        QTest::addColumn<int>("d"); QTest::addColumn<int>("h");
        QTest::addColumn<int>("mi"); QTest::addColumn<int>("s");
        QTest::addColumn<int>("field");
        QTest::newRow("year 1999")  << 1999 << 1 << 1 << 0 << 0 << 0 << int(FieldYear);
        QTest::newRow("year 2100")  << 2100 << 1 << 1 << 0 << 0 << 0 << int(FieldYear);
        QTest::newRow("year neg")   << -5 << 1 << 1 << 0 << 0 << 0 << int(FieldYear);
        QTest::newRow("month 0")    << 2026 << 0 << 1 << 0 << 0 << 0 << int(FieldMonth);
        QTest::newRow("month 13")   << 2026 << 13 << 1 << 0 << 0 << 0 << int(FieldMonth); // 5f case C
        QTest::newRow("day 0")      << 2026 << 1 << 0 << 0 << 0 << 0 << int(FieldDay);
        QTest::newRow("day 32")     << 2026 << 10 << 32 << 0 << 0 << 0 << int(FieldDay);  // 5f case D
        QTest::newRow("apr 31")     << 2026 << 4 << 31 << 0 << 0 << 0 << int(FieldDay);
        QTest::newRow("feb 29 2026") << 2026 << 2 << 29 << 0 << 0 << 0 << int(FieldDay);
        QTest::newRow("feb 29 2100") << 2100 << 2 << 29 << 0 << 0 << 0 << int(FieldYear);
        QTest::newRow("hour 24")    << 2026 << 1 << 1 << 24 << 0 << 0 << int(FieldHour);
        QTest::newRow("hour 25")    << 2026 << 1 << 1 << 25 << 0 << 0 << int(FieldHour);  // 5f case E
        QTest::newRow("hour -1")    << 2026 << 1 << 1 << -1 << 0 << 0 << int(FieldHour);
        QTest::newRow("minute 60")  << 2026 << 1 << 1 << 0 << 60 << 0 << int(FieldMinute); // 5f case F
        QTest::newRow("second 60")  << 2026 << 1 << 1 << 0 << 0 << 60 << int(FieldSecond);
        QTest::newRow("second -1")  << 2026 << 1 << 1 << 0 << 0 << -1 << int(FieldSecond);
        QTest::newRow("first wins") << 2026 << 13 << 40 << 99 << 99 << 99 << int(FieldMonth);
    }
    void invalid()
    {
        QFETCH(int, y); QFETCH(int, mo); QFETCH(int, d); QFETCH(int, h);
        QFETCH(int, mi); QFETCH(int, s); QFETCH(int, field);
        DateTime dt(y, mo, d, h, mi, s);
        QCOMPARE(int(validateDateTime(dt)), field);
        TimeFrame tf = buildTimeFrame(dt);
        QVERIFY(!tf.ok());
        QCOMPARE(int(tf.invalidField), field);
        QVERIFY(tf.bytes.empty());
    }
    void validEdges()
    {
        QVERIFY(buildTimeFrame(DateTime(2028, 2, 29, 23, 59, 59)).ok()); // leap
        QVERIFY(buildTimeFrame(DateTime(2000, 1, 1, 0, 0, 0)).ok());
        QVERIFY(buildTimeFrame(DateTime(2099, 12, 31, 23, 59, 59)).ok());
        QVERIFY(buildTimeFrame(DateTime(2000, 2, 29, 0, 0, 0)).ok());     // 400-year rule
        QVERIFY(!buildTimeFrame(DateTime(2000, 2, 30, 0, 0, 0)).ok());
    }

    void decodeRejects_data()
    {
        QTest::addColumn<QByteArray>("frame");
        QTest::newRow("8 bytes")      << QByteArray::fromHex("09ea070a18100421");           // 5f case A
        QTest::newRow("12 bytes")     << QByteArray::fromHex("09ea070a18100421060001" "00"); // case B
        QTest::newRow("empty")        << QByteArray();
        QTest::newRow("wrong lead")   << QByteArray::fromHex("39ea070a18100421060001");
        QTest::newRow("wrong tail 1") << QByteArray::fromHex("09ea070a18100421060101");
        QTest::newRow("wrong tail 2") << QByteArray::fromHex("09ea070a18100421060000");
        QTest::newRow("month 13")     << QByteArray::fromHex("09ea070d18100421060001");
        QTest::newRow("hour 25")      << QByteArray::fromHex("09ea070a18190421060001");
        QTest::newRow("dow 9")        << QByteArray::fromHex("09ea070a18100421090001");     // case G
        QTest::newRow("dow 8")        << QByteArray::fromHex("09ea070a18100421080001");
    }
    void decodeRejects()
    {
        QFETCH(QByteArray, frame);
        Bytes b(frame.constData(), frame.constData() + frame.size());
        DateTime dt; int dow;
        QVERIFY(!decodeTimeFrame(b, &dt, &dow));
    }
};

class TestUtf8 : public QObject
{
    Q_OBJECT
private slots:
    void validity_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<bool>("valid");
        QTest::newRow("ascii")       << QByteArray("hello") << true;
        QTest::newRow("empty")       << QByteArray() << true;
        QTest::newRow("accents")     << QString::fromUtf8("à è ì ò ù € ñ ü").toUtf8() << true;
        QTest::newRow("4 byte")      << QByteArray::fromHex("f09f9880") << true;
        QTest::newRow("lone cont")   << QByteArray::fromHex("80") << false;
        QTest::newRow("bad cont")    << QByteArray::fromHex("c328") << false;
        QTest::newRow("truncated 2") << QByteArray::fromHex("c3") << false;
        QTest::newRow("truncated 3") << QByteArray::fromHex("e282") << false;
        QTest::newRow("truncated 4") << QByteArray::fromHex("f09f98") << false;
        QTest::newRow("overlong 2")  << QByteArray::fromHex("c080") << false;
        QTest::newRow("overlong 3")  << QByteArray::fromHex("e08080") << false;
        QTest::newRow("surrogate")   << QByteArray::fromHex("eda080") << false;
        QTest::newRow("above 10ffff") << QByteArray::fromHex("f4908080") << false;
        QTest::newRow("0xff")        << QByteArray::fromHex("ff") << false;
    }
    void validity()
    {
        QFETCH(QByteArray, text); QFETCH(bool, valid);
        QCOMPARE(isValidUtf8(std::string(text.constData(), size_t(text.size()))), valid);
    }

    // The characters named in the à è ì ò ù € ñ ü (2,2,2,2,2,3,2,2 bytes).
    void truncateByChars()
    {
        const std::string s = QString::fromUtf8("àèìòù€ñü").toUtf8().constData();
        QString full = QString::fromUtf8("àèìòù€ñü");
        QCOMPARE(countCodePoints(s), size_t(8));
        for (int n = 0; n <= 10; ++n) {
            std::string t = truncateUtf8(s, size_t(n), 1000);
            QVERIFY(isValidUtf8(t));
            QCOMPARE(QString::fromUtf8(t.c_str()), full.left(n));
        }
    }
    void truncateByBytesNeverCutsACharacter()
    {
        const std::string s = QString::fromUtf8("àèìòù€ñü").toUtf8().constData();
        QString full = QString::fromUtf8("àèìòù€ñü");
        // à è ì ò ù = bytes 0..10, € = 10..13, ñ = 13..15, ü = 15..17
        const size_t expectChars[] = {0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5,
                                      5, 5, 6, 6, 7, 7, 8, 8};
        for (size_t n = 0; n <= 18; ++n) {
            std::string t = truncateUtf8(s, 1000, n);
            QVERIFY2(isValidUtf8(t), qPrintable(QString::number(n)));
            QVERIFY(t.size() <= n);
            QCOMPARE(QString::fromUtf8(t.c_str()), full.left(int(expectChars[n])));
        }
    }
    void truncateFourByte()
    {
        const std::string s = repeat("\xF0\x9F\x98\x80", 3); // 3 x U+1F600
        QCOMPARE(truncateUtf8(s, 10, 7).size(), size_t(4));
        QCOMPARE(truncateUtf8(s, 10, 3).size(), size_t(0));
        QCOMPARE(truncateUtf8(s, 2, 100).size(), size_t(8));
    }
    void truncateInvalidEndsText()
    {
        std::string t = truncateUtf8(str("ab\xC3(cd"), 10, 100);
        QCOMPARE(t, str("ab"));
        QVERIFY(isValidUtf8(t));
        QCOMPARE(truncateUtf8(str("ab\xC3"), 10, 100), str("ab")); // cut sequence
    }
};

class TestNotification : public QObject
{
    Q_OBJECT
private slots:
    // delete of id 9001, timestamp 20261024T170946, as sent.
    void goldenDelete()
    {
        const Bytes wire = hex("d6dcfffffdffffcdcfcdc9cecfcdcbabcec8cfc6cbc9ffffffffffffffff");
        QCOMPARE(wire.size(), size_t(30));

        NotifFrame f = buildDeleteFrame(9001, DateTime(2026, 10, 24, 17, 9, 46));
        QVERIFY(f.ok());
        QCOMPARE(toQ(f.bytes), toQ(wire));
        QCOMPARE(f.bytes.size(), kDeleteFrameSize);

        DecodedFrame d;
        QVERIFY(decodeFrame(wire, &d));
        QCOMPARE(d.id, uint32_t(9001));
        QCOMPARE(int(d.deleteFlag), 0x02);
        QCOMPARE(int(d.alert), 0);
        QCOMPARE(int(d.category), 0);
        QCOMPARE(d.timestamp.year, 2026); QCOMPARE(d.timestamp.month, 10);
        QCOMPARE(d.timestamp.day, 24); QCOMPARE(d.timestamp.hour, 17);
        QCOMPARE(d.timestamp.minute, 9); QCOMPARE(d.timestamp.second, 46);
        QVERIFY(d.sender.empty() && d.title.empty() && d.subtitle.empty() && d.message.empty());
    }
    void deleteFrameRejectsBadTimestamp()
    {
        QVERIFY(!buildDeleteFrame(1, DateTime(2026, 13, 1, 0, 0, 0)).ok());
        QCOMPARE(int(buildDeleteFrame(1, DateTime(2026, 13, 1, 0, 0, 0)).error), int(NotifBadTimestamp));
    }

    // N4: id 9004, calendar, 57 bytes.
    void goldenNotification()
    {
        const Bytes wire = hex("d3dcfffffffffacdcfcdc9cecfcdcbabcec8cfcccccdfcffbc9e93f8ffb29a9a8b969198f4ffcecfc5cfcfd2cecec5cfcff9ffad909092dfcb");
        QCOMPARE(wire.size(), size_t(57));

        Notification n = makeNotif();
        n.sender = "Cal"; n.title = "Meeting"; n.subtitle = "10:00-11:00"; n.message = "Room 4";
        NotifFrame f = buildNotificationFrame(n);
        QVERIFY(f.ok());
        QCOMPARE(toQ(f.bytes), toQ(wire));

        DecodedFrame d;
        QVERIFY(decodeFrame(wire, &d));
        QCOMPARE(d.id, uint32_t(9004));
        QCOMPARE(int(d.deleteFlag), 0);
        QCOMPARE(int(d.alert), 0);
        QCOMPARE(int(d.category), 5);
        QCOMPARE(d.timestamp.hour, 17); QCOMPARE(d.timestamp.minute, 3);
        QCOMPARE(d.timestamp.second, 32);
        QCOMPARE(d.sender, str("Cal"));
        QCOMPARE(d.title, str("Meeting"));
        QCOMPARE(d.subtitle, str("10:00-11:00"));
        QCOMPARE(d.message, str("Room 4"));
    }

    void headerLayout()
    {
        Notification n = makeNotif();
        n.id = 0x01020304; n.alert = true; n.category = kCategoryChat;
        Bytes plain = invertBytes(buildNotificationFrame(n).bytes);
        QCOMPARE(plain.size(), size_t(30));
        QCOMPARE(int(plain[0]), 0x04); QCOMPARE(int(plain[1]), 0x03);
        QCOMPARE(int(plain[2]), 0x02); QCOMPARE(int(plain[3]), 0x01); // id LE
        QCOMPARE(int(plain[4]), 0);   // delete flag
        QCOMPARE(int(plain[5]), 1);   // alert
        QCOMPARE(int(plain[6]), 4);   // category
        QCOMPARE(std::string(plain.begin() + 7, plain.begin() + 22), str("20261024T170332"));
    }
    void invertIsItsOwnInverse()
    {
        Bytes b = hex("00017f80ff");
        QCOMPARE(toQ(invertBytes(b)), toQ(hex("ffFE807f00")));
        QCOMPARE(toQ(invertBytes(invertBytes(b))), toQ(b));
    }

    // N5: 68 bytes. N6: 214 bytes, the largest accepted.
    void sizesFromTheSpike()
    {
        Notification n5 = makeNotif();
        n5.category = 0;
        n5.sender = "UTF8";
        n5.title = QString::fromUtf8("àèìòù €").toUtf8().constData();
        n5.message = QString::fromUtf8("Cafés cliché ñ ü").toUtf8().constData();
        QCOMPARE(buildNotificationFrame(n5).bytes.size(), size_t(68));

        Notification n6 = makeNotif();
        n6.category = 0;
        n6.sender = repeat("S", 32); n6.title = repeat("T", 32);
        n6.subtitle = repeat("U", 20); n6.message = repeat("M", 100);
        NotifFrame f6 = buildNotificationFrame(n6);
        QVERIFY(f6.ok());
        QCOMPARE(f6.bytes.size(), size_t(214));
        QCOMPARE(kMaxFrameBytes, size_t(214));
    }
    void oversizeIsRefused()
    {
        Notification n = makeNotif();
        n.message = repeat("M", 184);          // 30 + 184 = 214: still fine
        QVERIFY(buildNotificationFrame(n).ok());
        n.message = repeat("M", 185);          // 215
        NotifFrame f = buildNotificationFrame(n);
        QVERIFY(!f.ok());
        QCOMPARE(int(f.error), int(NotifTooLarge));
        QVERIFY(f.bytes.empty());
        n.message = repeat("M", 250);          // N7 was 286 B
        QCOMPARE(int(buildNotificationFrame(n).error), int(NotifTooLarge));
    }

    void validation()
    {
        Notification n = makeNotif();
        n.category = kMaxCategory;
        QVERIFY(buildNotificationFrame(n).ok());
        n.category = kMaxCategory + 1;
        QCOMPARE(int(buildNotificationFrame(n).error), int(NotifBadCategory));
        n.category = 0;

        n.timestamp = DateTime(2026, 2, 30, 0, 0, 0);
        QCOMPARE(int(buildNotificationFrame(n).error), int(NotifBadTimestamp));
        n.timestamp = DateTime(2026, 10, 24, 25, 0, 0);
        QCOMPARE(int(buildNotificationFrame(n).error), int(NotifBadTimestamp));
        n.timestamp = makeNotif().timestamp;

        const char *bad[] = {"\xC3(", "\x80", "\xC0\x80", "\xED\xA0\x80", "\xF4\x90\x80\x80", "ab\xE2\x82"};
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
            Notification m = makeNotif();
            m.sender = bad[i];
            QCOMPARE(int(buildNotificationFrame(m).error), int(NotifBadUtf8));
            m = makeNotif(); m.title = bad[i];
            QCOMPARE(int(buildNotificationFrame(m).error), int(NotifBadUtf8));
            m = makeNotif(); m.subtitle = bad[i];
            QCOMPARE(int(buildNotificationFrame(m).error), int(NotifBadUtf8));
            m = makeNotif(); m.message = bad[i];
            QCOMPARE(int(buildNotificationFrame(m).error), int(NotifBadUtf8));
        }
    }

    void roundTrip_data()
    {
        QTest::addColumn<QString>("sender"); QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("subtitle"); QTest::addColumn<QString>("message");
        QTest::addColumn<bool>("alert"); QTest::addColumn<int>("category");
        const QString e;
        QTest::newRow("empty")  << e << e << e << e << false << 0;
        QTest::newRow("ascii")  << QString("Alex") << e << e << QString("Hello from Jolla") << true << 4;
        QTest::newRow("utf8")   << QString::fromUtf8("Cafés") << QString::fromUtf8("àèìòù €") << QString::fromUtf8("ñ") << QString::fromUtf8("cliché ü") << false << 6;
        QTest::newRow("4byte")  << QString::fromUtf8("\xF0\x9F\x98\x80") << QString("t") << e << QString("m") << true << 13;
    }
    void roundTrip()
    {
        QFETCH(QString, sender); QFETCH(QString, title);
        QFETCH(QString, subtitle); QFETCH(QString, message);
        QFETCH(bool, alert); QFETCH(int, category);
        Notification n = makeNotif();
        n.id = 0xFFFFFFFFu; n.alert = alert; n.category = uint8_t(category);
        n.sender = sender.toUtf8().constData();
        n.title = title.toUtf8().constData();
        n.subtitle = subtitle.toUtf8().constData();
        n.message = message.toUtf8().constData();

        NotifFrame f = buildNotificationFrame(n);
        QVERIFY(f.ok());
        DecodedFrame d;
        QVERIFY(decodeFrame(invertBytes(invertBytes(f.bytes)), &d));
        QCOMPARE(d.id, n.id);
        QCOMPARE(bool(d.alert), alert);
        QCOMPARE(int(d.category), category);
        QCOMPARE(int(d.deleteFlag), 0);
        QCOMPARE(d.sender, n.sender); QCOMPARE(d.title, n.title);
        QCOMPARE(d.subtitle, n.subtitle); QCOMPARE(d.message, n.message);
        QCOMPARE(d.timestamp.second, n.timestamp.second);

        // re-encode the decoded content: same bytes
        Notification m = makeNotif();
        m.id = d.id; m.alert = d.alert != 0; m.category = d.category;
        m.timestamp = d.timestamp;
        m.sender = d.sender; m.title = d.title; m.subtitle = d.subtitle; m.message = d.message;
        QCOMPARE(toQ(buildNotificationFrame(m).bytes), toQ(f.bytes));
    }

    void decodeRejects()
    {
        const Bytes good = hex("d3dcfffffffffacdcfcdc9cecfcdcbabcec8cfcccccdfcffbc9e93f8ffb29a9a8b969198f4ffcecfc5cfcfd2cecec5cfcff9ffad909092dfcb");
        DecodedFrame d;
        QVERIFY(decodeFrame(good, &d));

        Bytes shortF(good.begin(), good.end() - 1);          // lengths overrun
        QVERIFY(!decodeFrame(shortF, &d));
        Bytes longF = good; longF.push_back(0xff);           // trailing byte
        QVERIFY(!decodeFrame(longF, &d));
        QVERIFY(!decodeFrame(Bytes(), &d));
        QVERIFY(!decodeFrame(Bytes(29, 0xff), &d));          // below minimum
        Bytes badTs = good;                                   // month digits -> 99
        badTs[11] = uint8_t(~uint8_t('9')); badTs[12] = uint8_t(~uint8_t('9'));
        QVERIFY(!decodeFrame(badTs, &d));
        Bytes noT = good; noT[15] = uint8_t(~uint8_t('X'));   // 'T' separator
        QVERIFY(!decodeFrame(noT, &d));
        Bytes badUtf = good; badUtf[24] = uint8_t(~0x80);     // 'C' of Cal -> 0x80
        QVERIFY(!decodeFrame(badUtf, &d));
    }

    // ----------------------------------------------------------- truncation
    void fitLeavesShortTextAlone()
    {
        Notification n = makeNotif();
        n.sender = "Cal"; n.title = "Meeting"; n.subtitle = "10:00-11:00"; n.message = "Room 4";
        Notification f = fitToWatch(n);
        QCOMPARE(f.sender, n.sender); QCOMPARE(f.title, n.title);
        QCOMPARE(f.subtitle, n.subtitle); QCOMPARE(f.message, n.message);
        QCOMPARE(f.id, n.id); QCOMPARE(f.alert, n.alert);
        QCOMPARE(f.category, n.category);
        QCOMPARE(f.timestamp.second, n.timestamp.second);
    }
    // No display limit: only the frame size of 214 B limits the text, 184 B for the four fields.
    void fitHasNoLineLimit()
    {
        // N6 (32+32+20+100 = 184 bytes) now goes out whole.
        Notification n6 = makeNotif();
        n6.sender = repeat("S", 32); n6.title = repeat("T", 32);
        n6.subtitle = repeat("U", 20); n6.message = repeat("M", 100);
        Notification f = fitToWatch(n6);
        QCOMPARE(f.sender, n6.sender); QCOMPARE(f.title, n6.title);
        QCOMPARE(f.subtitle, n6.subtitle); QCOMPARE(f.message, n6.message);
        QCOMPARE(buildNotificationFrame(f).bytes.size(), size_t(214));
    }
    void fitNewlinesPassThrough()
    {
        Notification n = makeNotif();
        n.message = "l1\nl2\nl3\nl4\nl5\nl6\n\n\nl9";
        Notification f = fitToWatch(n);
        QCOMPARE(f.message, n.message);
        QVERIFY(buildNotificationFrame(f).ok());
        DecodedFrame d;
        QVERIFY(decodeFrame(buildNotificationFrame(f).bytes, &d));
        QCOMPARE(d.message, n.message);
    }
    void fitByteBoundInDisplayOrder()
    {
        Notification n = makeNotif();
        n.sender = repeat("s", 100); n.title = repeat("t", 100);
        n.subtitle = "sub"; n.message = "msg";
        Notification f = fitToWatch(n);
        QCOMPARE(f.sender, repeat("s", 100));
        QCOMPARE(f.title, repeat("t", 84));      // 184 - 100
        QVERIFY(f.subtitle.empty() && f.message.empty());
        QCOMPARE(buildNotificationFrame(f).bytes.size(), kMaxFrameBytes);

        Notification a = makeNotif();
        a.message = repeat("m", 250);            // N7 case
        f = fitToWatch(a);
        QCOMPARE(f.message.size(), size_t(184));
        QVERIFY(buildNotificationFrame(f).ok());
    }
    void fitCutsOnCharacterBoundaries()
    {
        Notification n = makeNotif();
        n.message = repeat("\xC3\xA0", 100);            // 100 x 'a grave' = 200 bytes
        Notification f = fitToWatch(n);
        QVERIFY(isValidUtf8(f.message));
        QCOMPARE(countCodePoints(f.message), size_t(92)); // 184 bytes
        n.message = repeat("\xF0\x9F\x98\x80", 64);    // 256 bytes
        f = fitToWatch(n);
        QVERIFY(isValidUtf8(f.message));
        QCOMPARE(f.message.size(), size_t(184));          // 46 characters
        QVERIFY(buildNotificationFrame(f).ok());
    }
    void fitIsAlwaysEncodable()
    {
        // Mixed 1-4 byte characters, varied lengths: output always builds,
        // fits the bound, stays valid UTF-8 and is a prefix of the input.
        const char *units[] = {"a", "\xC3\xA0", "\xE2\x82\xAC", "\xF0\x9F\x98\x80", "\n"};
        unsigned seed = 12345;
        for (int iter = 0; iter < 400; ++iter) {
            Notification n = makeNotif();
            std::string *fields[4] = {&n.sender, &n.title, &n.subtitle, &n.message};
            for (int k = 0; k < 4; ++k) {
                seed = seed * 1103515245u + 12345u;
                int len = int((seed >> 16) % 90);
                if ((seed >> 8) % 5 == 0) len = 0;
                for (int i = 0; i < len; ++i) {
                    seed = seed * 1103515245u + 12345u;
                    *fields[k] += units[(seed >> 16) % 5];
                }
            }
            Notification f = fitToWatch(n);
            NotifFrame fr = buildNotificationFrame(f);
            QVERIFY(fr.ok());
            QVERIFY(fr.bytes.size() <= kMaxFrameBytes);
            QVERIFY(n.sender.compare(0, f.sender.size(), f.sender) == 0);
            QVERIFY(n.title.compare(0, f.title.size(), f.title) == 0);
            QVERIFY(n.subtitle.compare(0, f.subtitle.size(), f.subtitle) == 0);
            QVERIFY(n.message.compare(0, f.message.size(), f.message) == 0);
        }
    }
    void normaliseCases_data()
    {
        QTest::addColumn<QByteArray>("in");
        QTest::addColumn<QByteArray>("out");
        QTest::newRow("empty") << QByteArray() << QByteArray();
        QTest::newRow("crlf") << QByteArray("a\r\nb") << QByteArray("a\nb");
        QTest::newRow("lone cr") << QByteArray("a\rb") << QByteArray("a\nb");
        QTest::newRow("tab") << QByteArray("a\tb\t") << QByteArray("ab");
        QTest::newRow("lf kept") << QByteArray("a\n\nb") << QByteArray("a\n\nb");
        QTest::newRow("two crlf") << QByteArray("\r\n\r\n") << QByteArray("\n\n");
        QTest::newRow("cr cr lf") << QByteArray("\r\r\n") << QByteArray("\n\n");
        QTest::newRow("lf cr") << QByteArray("\n\r") << QByteArray("\n\n");
        QTest::newRow("cr tab lf") << QByteArray("\r\t\n") << QByteArray("\n\n");
        // the test message
        QTest::newRow("5j") << QByteArray("A\rB\tC\r\nD") << QByteArray("A\nBC\nD");
        QTest::newRow("utf8 kept") << QByteArray("\xC3\xA0\t\xE2\x82\xAC\r")
                                   << QByteArray("\xC3\xA0\xE2\x82\xAC\n");
        QTest::newRow("invalid kept") << QByteArray("ok\xC3(\t") << QByteArray("ok\xC3(");
        QTest::newRow("other controls kept") << QByteArray("a\x01\x0b\x0c" "b")
                                             << QByteArray("a\x01\x0b\x0c" "b");
    }
    void normaliseCases()
    {
        QFETCH(QByteArray, in);
        QFETCH(QByteArray, out);
        std::string r = normaliseText(std::string(in.constData(), size_t(in.size())));
        QCOMPARE(QByteArray(r.data(), int(r.size())), out);
        // idempotent
        QCOMPARE(normaliseText(r), r);
    }
    void normaliseIdempotentRandom()
    {
        const char *units[] = {"a", "\r", "\n", "\t", "\r\n", "\xC3\xA0"};
        unsigned seed = 777;
        for (int iter = 0; iter < 300; ++iter) {
            std::string s;
            int len = int(seed % 40);
            for (int i = 0; i < len; ++i) {
                seed = seed * 1103515245u + 12345u;
                s += units[(seed >> 16) % 6];
            }
            std::string once = normaliseText(s);
            QCOMPARE(normaliseText(once), once);
            QVERIFY(once.find('\r') == std::string::npos);
            QVERIFY(once.find('\t') == std::string::npos);
        }
    }
    void fitNormalisesAllFourFields()
    {
        Notification n = makeNotif();
        n.sender = "a\tb"; n.title = "c\r\nd"; n.subtitle = "e\rf"; n.message = "g\th\r\ni";
        Notification f = fitToWatch(n);
        QCOMPARE(f.sender, str("ab")); QCOMPARE(f.title, str("c\nd"));
        QCOMPARE(f.subtitle, str("e\nf")); QCOMPARE(f.message, str("gh\ni"));
        // fitToWatch twice = once
        Notification g = fitToWatch(f);
        QCOMPARE(g.message, f.message); QCOMPARE(g.title, f.title);
    }
    void fitBudgetIsMeasuredAfterNormalising()
    {
        // 184 'a' plus 100 tabs and CR LF pairs: raw size is far over the
        // budget, normalised size is exactly 184 + 0.
        Notification n = makeNotif();
        n.message = repeat("a", 90) + repeat("\t", 100) + repeat("\r\n", 20) + repeat("b", 54);
        Notification f = fitToWatch(n);
        // 90 + 20 newlines + 54 = 164 bytes: everything fits
        QCOMPARE(f.message.size(), size_t(164));
        QVERIFY(f.message.find('\t') == std::string::npos);
        QVERIFY(buildNotificationFrame(f).ok());
        // normalised text of exactly 184 bytes passes whole
        n.message = repeat("a", 184) + repeat("\t", 10);
        f = fitToWatch(n);
        QCOMPARE(f.message, repeat("a", 184));
        // a CR LF at the cut counts once
        n.message = repeat("a", 183) + "\r\nZZ";
        f = fitToWatch(n);
        QCOMPARE(f.message, repeat("a", 183) + "\n");
    }
    void fitDropsInvalidTail()
    {
        Notification n = makeNotif();
        n.message = "ok\xC3(bad";
        Notification f = fitToWatch(n);
        QCOMPARE(f.message, str("ok"));
        QVERIFY(buildNotificationFrame(f).ok());
    }
};

class TestModelTable : public QObject
{
    Q_OBJECT
    static QByteArray sample()
    {
        return QByteArray(
            "{ \"version\": 1, \"models\": ["
            "  { \"id\": \"dw-h5600\", \"label\": \"Casio DW-H5600\","
            "    \"family\": \"h5600\", \"status\": \"supported\","
            "    \"namePatterns\": [\"CASIO DW-H5600\"],"
            "    \"params\": { \"dowEncoding\": \"sun0\" } },"
            "  { \"id\": \"fixture-mon\", \"label\": \"Fixture\","
            "    \"family\": \"fixture\", \"status\": \"untested\","
            "    \"namePatterns\": [\"CASIO FIX-[0-9]+\", \"FIX Watch\"],"
            "    \"params\": { \"dowEncoding\": \"mon1\" } },"
            "  { \"id\": \"fixture-any\", \"label\": \"Any CASIO\","
            "    \"family\": \"fixture\", \"status\": \"untested\","
            "    \"namePatterns\": [\"CASIO .*\"],"
            "    \"params\": { \"dowEncoding\": \"sun0\" } }"
            "] }");
    }
    static QByteArray with(const char *models)
    {
        return QByteArray("{ \"version\": 1, \"models\": [") + models + "] }";
    }
private slots:
    void loadsAndFinds()
    {
        ModelTable t; QString err;
        QVERIFY2(t.load(sample(), &err), qPrintable(err));
        QCOMPARE(t.count(), 3);
        const ModelInfo *m = t.find("CASIO DW-H5600");
        QVERIFY(m);
        QCOMPARE(m->id, QString("dw-h5600"));
        QCOMPARE(m->label, QString("Casio DW-H5600"));
        QCOMPARE(m->family, QString("h5600"));
        QCOMPARE(int(m->status), int(ModelSupported));
        QCOMPARE(int(m->dowEncoding), int(DowSun0));
    }
    void matchesWholeNameOnly()
    {
        ModelTable t; QVERIFY(t.load(sample()));
        QCOMPARE(t.find("CASIO DW-H5600")->id, QString("dw-h5600"));
        // not a full match of the first entry: falls to the catch-all
        QCOMPARE(t.find("CASIO DW-H56001")->id, QString("fixture-any"));
        QCOMPARE(t.find("xCASIO DW-H5600"), static_cast<const ModelInfo *>(0));
        QCOMPARE(t.find(""), static_cast<const ModelInfo *>(0));
        QCOMPARE(t.find("Some Headset"), static_cast<const ModelInfo *>(0));
    }
    void firstMatchWinsAndPerModelParameters()
    {
        ModelTable t; QVERIFY(t.load(sample()));
        const ModelInfo *m = t.find("CASIO FIX-42");
        QVERIFY(m);
        QCOMPARE(m->id, QString("fixture-mon"));          // before the catch-all
        QCOMPARE(int(m->dowEncoding), int(DowMon1));
        QCOMPARE(int(m->status), int(ModelUntested));
        QCOMPARE(t.find("FIX Watch")->id, QString("fixture-mon")); // second pattern
        QCOMPARE(t.find("CASIO GBD-200")->id, QString("fixture-any"));
    }
    void dowDefaultsToSun0WhenParamsAbsent()
    {
        ModelTable t;
        QVERIFY(t.load(with("{\"id\":\"a\",\"label\":\"A\",\"family\":\"f\","
                            "\"status\":\"untested\",\"namePatterns\":[\"A\"]}")));
        QCOMPARE(int(t.find("A")->dowEncoding), int(DowSun0));
    }
    void defaultSilhouetteIsOptional()
    {
        ModelTable t;
        QVERIFY(t.load(with("{\"id\":\"a\",\"label\":\"A\",\"family\":\"f\","
                            "\"status\":\"untested\",\"namePatterns\":[\"A\"],"
                            "\"defaultSilhouette\":\"round\",\"futureKey\":1},"
                            "{\"id\":\"b\",\"label\":\"B\",\"family\":\"f\","
                            "\"status\":\"untested\",\"namePatterns\":[\"B\"]},"
                            "{\"id\":\"c\",\"label\":\"C\",\"family\":\"f\","
                            "\"status\":\"untested\",\"namePatterns\":[\"C\"],"
                            "\"defaultSilhouette\":7}")));
        QCOMPARE(t.find("A")->defaultSilhouette, QString("round"));
        QCOMPARE(t.find("B")->defaultSilhouette, QString());      // absent
        QCOMPARE(t.find("C")->defaultSilhouette, QString());      // not a string: absent
        QVERIFY(t.load(sample()));
        QCOMPARE(t.find("CASIO FIX-1")->defaultSilhouette, QString());
    }
    void caseSensitive()
    {
        ModelTable t; QVERIFY(t.load(sample()));
        QCOMPARE(t.find("casio dw-h5600"), static_cast<const ModelInfo *>(0));
    }
    void usesTableForTheFrame()
    {
        ModelTable t; QVERIFY(t.load(sample()));
        DateTime sunday(2026, 10, 25, 12, 0, 0);
        QCOMPARE(int(buildTimeFrame(sunday, t.find("CASIO DW-H5600")->dowEncoding).bytes[8]), 0);
        QCOMPARE(int(buildTimeFrame(sunday, t.find("CASIO FIX-1")->dowEncoding).bytes[8]), 7);
    }

    void rejects_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::newRow("not json")       << QByteArray("models");
        QTest::newRow("not an object")  << QByteArray("[]");
        QTest::newRow("no version")     << QByteArray("{\"models\":[]}");
        QTest::newRow("version 2")      << QByteArray("{\"version\":2,\"models\":[]}");
        QTest::newRow("no models")      << QByteArray("{\"version\":1}");
        QTest::newRow("models not array") << QByteArray("{\"version\":1,\"models\":{}}");
        QTest::newRow("missing id")     << with("{\"label\":\"A\",\"family\":\"f\",\"status\":\"supported\",\"namePatterns\":[\"A\"]}");
        QTest::newRow("empty id")       << with("{\"id\":\"\",\"label\":\"A\",\"family\":\"f\",\"status\":\"supported\",\"namePatterns\":[\"A\"]}");
        QTest::newRow("missing family") << with("{\"id\":\"a\",\"label\":\"A\",\"status\":\"supported\",\"namePatterns\":[\"A\"]}");
        QTest::newRow("bad status")     << with("{\"id\":\"a\",\"label\":\"A\",\"family\":\"f\",\"status\":\"maybe\",\"namePatterns\":[\"A\"]}");
        QTest::newRow("no patterns")    << with("{\"id\":\"a\",\"label\":\"A\",\"family\":\"f\",\"status\":\"supported\",\"namePatterns\":[]}");
        QTest::newRow("pattern not string") << with("{\"id\":\"a\",\"label\":\"A\",\"family\":\"f\",\"status\":\"supported\",\"namePatterns\":[1]}");
        QTest::newRow("bad regex")      << with("{\"id\":\"a\",\"label\":\"A\",\"family\":\"f\",\"status\":\"supported\",\"namePatterns\":[\"(\"]}");
        QTest::newRow("bad dow")        << with("{\"id\":\"a\",\"label\":\"A\",\"family\":\"f\",\"status\":\"supported\",\"namePatterns\":[\"A\"],\"params\":{\"dowEncoding\":\"tue2\"}}");
        QTest::newRow("duplicate id")   << with("{\"id\":\"a\",\"label\":\"A\",\"family\":\"f\",\"status\":\"supported\",\"namePatterns\":[\"A\"]},"
                                                "{\"id\":\"a\",\"label\":\"B\",\"family\":\"f\",\"status\":\"supported\",\"namePatterns\":[\"B\"]}");
    }
    void rejects()
    {
        QFETCH(QByteArray, json);
        ModelTable t;
        QVERIFY(t.load(sample()));            // a failed load empties the table
        QString err;
        QVERIFY(!t.load(json, &err));
        QVERIFY(!err.isEmpty());
        QCOMPARE(t.count(), 0);
        QCOMPARE(t.find("CASIO DW-H5600"), static_cast<const ModelInfo *>(0));
    }
    void emptyListIsValid()
    {
        ModelTable t; QVERIFY(t.load(QByteArray("{\"version\":1,\"models\":[]}")));
        QCOMPARE(t.count(), 0);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int status = 0;
    { TestTimeFrame t;    status |= QTest::qExec(&t, argc, argv); }
    { TestUtf8 t;         status |= QTest::qExec(&t, argc, argv); }
    { TestNotification t; status |= QTest::qExec(&t, argc, argv); }
    { TestModelTable t;   status |= QTest::qExec(&t, argc, argv); }
    return status;
}

#include "tst_watchproto.moc"
