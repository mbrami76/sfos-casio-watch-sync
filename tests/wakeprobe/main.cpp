// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// wakeprobe: on-phone probe for the wake-ups. Asks the wrapper for a wake-up every N
// seconds and appends one line per wake to a log: wall clock, CLOCK_BOOTTIME (counts while
// suspended) and CLOCK_MONOTONIC (stops while suspended). The difference between the two shows how
// long the phone slept; the gap between lines shows how late the system woke the process. No
// Bluetooth, no network, no notification text.
//
// Usage: wakeprobe [seconds=300] [wakes=0 (forever)] [logfile=~/wakeprobe.log] wakeprobe replace
// [long=600] [short=60] [logfile] arms a long wait, then at once a short one, and logs when it
// fires: "ok" if the short request replaced the long one. wakeprobe hold [seconds=120] [logfile]
// hold() the phone awake, wait with a plain timer, release(). "slept" near 0 means the phone did
// not suspend while held. Switch the screen off (power key) right after starting.
#include "scheduler.h"
#include "wakescheduler.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QTimer>

#include <stdio.h>
#include <time.h>

using namespace scheduler;

namespace {

double clockSeconds(clockid_t id)
{
    struct timespec ts;
    clock_gettime(id, &ts);
    return double(ts.tv_sec) + double(ts.tv_nsec) / 1e9;
}

void logLine(const QString &path, const QString &what)
{
    const QString line = QDateTime::currentDateTime().toString(Qt::ISODate)
                       + QLatin1Char(' ') + what;
    QFile f(path);
    if (f.open(QIODevice::Append | QIODevice::Text))
        QTextStream(&f) << line << "\n";
    printf("%s\n", qPrintable(line));
    fflush(stdout);
}

class Probe : public QObject
{
    Q_OBJECT
public:
    Probe(int seconds, int wakes, const QString &logPath)
        : m_seconds(seconds), m_wakes(wakes), m_count(0), m_log(logPath),
          m_wake(&m_clock), m_prevBoot(clockSeconds(CLOCK_BOOTTIME)),
          m_prevMono(clockSeconds(CLOCK_MONOTONIC)), m_prevWall(QDateTime::currentMSecsSinceEpoch())
    {
        m_wake.setMaxSleepSeconds(24 * 3600);   // the probe measures the request as given
        connect(&m_wake, SIGNAL(wake()), this, SLOT(onWake()));
        write(QStringLiteral("start requested=%1s range=[%2,%3]")
                  .arg(seconds).arg(wakeRangeFor(seconds, 24 * 3600).minSec)
                  .arg(wakeRangeFor(seconds, 24 * 3600).maxSec));
        arm();
    }

private slots:
    void onWake()
    {
        ++m_count;
        const double boot = clockSeconds(CLOCK_BOOTTIME), mono = clockSeconds(CLOCK_MONOTONIC);
        const qint64 wall = QDateTime::currentMSecsSinceEpoch();
        write(QStringLiteral("wake %1 dBoot=%2s dMono=%3s dWall=%4s slept=%5s")
                  .arg(m_count)
                  .arg(boot - m_prevBoot, 0, 'f', 1)
                  .arg(mono - m_prevMono, 0, 'f', 1)
                  .arg(double(wall - m_prevWall) / 1000.0, 0, 'f', 1)
                  .arg((boot - m_prevBoot) - (mono - m_prevMono), 0, 'f', 1));
        m_prevBoot = boot; m_prevMono = mono; m_prevWall = wall;
        if (m_wakes > 0 && m_count >= m_wakes) {
            write(QStringLiteral("done"));
            QCoreApplication::quit();
            return;
        }
        arm();
    }

private:
    void arm() { m_wake.schedule(m_clock.now().addSecs(m_seconds)); }

    void write(const QString &what) { logLine(m_log, what); }

    int m_seconds, m_wakes, m_count;
    QString m_log;
    SystemClock m_clock;
    BackgroundActivityWake m_wake;
    double m_prevBoot, m_prevMono;
    qint64 m_prevWall;
};

// Mode "replace": a long wait, then a short one before the long one ends.
class ReplaceProbe : public QObject
{
    Q_OBJECT
public:
    ReplaceProbe(int longSec, int shortSec, const QString &logPath)
        : m_long(longSec), m_short(shortSec), m_log(logPath), m_wake(&m_clock)
    {
        m_wake.setMaxSleepSeconds(24 * 3600);
        connect(&m_wake, SIGNAL(wake()), this, SLOT(onWake()));
        write(QStringLiteral("replace start long=%1s short=%2s").arg(longSec).arg(shortSec));
        m_wake.schedule(m_clock.now().addSecs(longSec));
        write(QStringLiteral("armed long, range=[%1,%2]").arg(m_wake.lastRange().minSec)
                                                          .arg(m_wake.lastRange().maxSec));
        QTimer::singleShot(1000, this, SLOT(armShort()));
    }

private slots:
    void armShort()
    {
        m_armed = QDateTime::currentMSecsSinceEpoch();
        m_bootArmed = clockSeconds(CLOCK_BOOTTIME);
        m_wake.schedule(m_clock.now().addSecs(m_short));
        write(QStringLiteral("armed short, range=[%1,%2]").arg(m_wake.lastRange().minSec)
                                                           .arg(m_wake.lastRange().maxSec));
    }
    void onWake()
    {
        const double elapsed = clockSeconds(CLOCK_BOOTTIME) - m_bootArmed;
        const bool ok = elapsed < double(m_long) / 2.0;
        write(QStringLiteral("wake after %1s since the short request (wall %2s): %3")
                  .arg(elapsed, 0, 'f', 1)
                  .arg(double(QDateTime::currentMSecsSinceEpoch() - m_armed) / 1000.0, 0, 'f', 1)
                  .arg(ok ? QStringLiteral("ok, the short request replaced the long one")
                          : QStringLiteral("FAIL, the long request was kept")));
        QCoreApplication::quit();
    }

private:
    void write(const QString &what) { logLine(m_log, what); }

    int m_long, m_short;
    QString m_log;
    qint64 m_armed;
    double m_bootArmed;
    SystemClock m_clock;
    BackgroundActivityWake m_wake;
};

// Mode "hold": the phone must not suspend while held.
class HoldProbe : public QObject
{
    Q_OBJECT
public:
    HoldProbe(int seconds, const QString &logPath)
        : m_seconds(seconds), m_log(logPath), m_wake(&m_clock),
          m_boot(clockSeconds(CLOCK_BOOTTIME)), m_mono(clockSeconds(CLOCK_MONOTONIC)),
          m_wall(QDateTime::currentMSecsSinceEpoch())
    {
        m_wake.hold();
        logLine(m_log, QStringLiteral("hold start seconds=%1 holds=%2 (switch the screen off now)")
                           .arg(seconds).arg(m_wake.holdCount()));
        QTimer::singleShot(seconds * 1000, this, SLOT(onTimer()));
    }

private slots:
    void onTimer()
    {
        const double boot = clockSeconds(CLOCK_BOOTTIME) - m_boot;
        const double mono = clockSeconds(CLOCK_MONOTONIC) - m_mono;
        const double wall = double(QDateTime::currentMSecsSinceEpoch() - m_wall) / 1000.0;
        const double slept = boot - mono;
        m_wake.release();
        logLine(m_log, QStringLiteral("hold end dBoot=%1s dMono=%2s dWall=%3s slept=%4s holds=%5: %6")
                           .arg(boot, 0, 'f', 1).arg(mono, 0, 'f', 1).arg(wall, 0, 'f', 1)
                           .arg(slept, 0, 'f', 1).arg(m_wake.holdCount())
                           .arg(slept < 2.0 ? QStringLiteral("ok, the phone stayed awake")
                                            : QStringLiteral("FAIL, the phone slept")));
        QCoreApplication::quit();
    }

private:
    int m_seconds;
    QString m_log;
    SystemClock m_clock;
    BackgroundActivityWake m_wake;
    double m_boot, m_mono;
    qint64 m_wall;
};

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QStringList a = app.arguments();
    const QString defLog = QDir::homePath() + QStringLiteral("/wakeprobe.log");
    if (a.size() > 1 && a.at(1) == QLatin1String("replace")) {
        ReplaceProbe p(a.size() > 2 ? qMax(120, a.at(2).toInt()) : 600,
                       a.size() > 3 ? qMax(30, a.at(3).toInt()) : 60,
                       a.size() > 4 ? a.at(4) : defLog);
        return app.exec();
    }
    if (a.size() > 1 && a.at(1) == QLatin1String("hold")) {
        HoldProbe p(a.size() > 2 ? qMax(10, a.at(2).toInt()) : 120, a.size() > 3 ? a.at(3) : defLog);
        return app.exec();
    }
    const int seconds = a.size() > 1 ? qMax(30, a.at(1).toInt()) : 300;
    const int wakes = a.size() > 2 ? qMax(0, a.at(2).toInt()) : 0;
    const QString log = a.size() > 3 ? a.at(3) : defLog;
    Probe probe(seconds, wakes, log);
    return app.exec();
}

#include "main.moc"
