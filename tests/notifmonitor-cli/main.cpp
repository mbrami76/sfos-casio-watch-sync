// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// notifmonitor-cli: on-phone test program for notifmonitor. Prints one line per notification
// published on the session bus: time, app_name, summary and body length (characters), replaces_id
// and the NAMES of the hints. It never prints notification text. Run it as the phone user, outside
// Sailjail.
//
// notifmonitor-cli one line per notification until Ctrl+C notifmonitor-cli --apps each distinct
// app_name once notifmonitor-cli --seconds N stop by itself after N seconds notifmonitor-cli
// --debug also the library's metadata debug log Lines go to stdout and are flushed at once; state
// messages go to stderr.
#include "notifmonitor.h"

#include <QCoreApplication>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <signal.h>
#include <stdio.h>

using namespace notifmonitor;

namespace {

class Printer : public QObject
{
    Q_OBJECT
public:
    explicit Printer(bool appsOnly) : m_appsOnly(appsOnly) {}

public slots:
    void onNotification(const notiffilter::IncomingNotification &n)
    {
        if (m_appsOnly) {
            if (m_seen.contains(n.appName))
                return;
            m_seen.insert(n.appName);
            print("%s\n", n.appName);
            return;
        }
        QStringList names = n.hints.keys();
        names.sort();
        print("%s app_name=\"%s\" summary_len=%d body_len=%d replaces_id=%u hints=%s\n",
              n.when.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")), n.appName,
              n.summary.size(), n.body.size(), n.replacesId, names.join(QLatin1Char(',')));
    }

    void onState(int state, const QString &reason)
    {
        const char *name = state == NotifMonitor::Running ? "running"
                         : state == NotifMonitor::CannotMonitor ? "cannot monitor" : "not running";
        fprintf(stderr, "state: %s%s%s\n", name, reason.isEmpty() ? "" : " - ",
                qPrintable(reason));
        fflush(stderr);
    }

private:
    // Small helpers so each argument type is formatted in one place.
    static void print(const char *fmt, const QString &a) { printf(fmt, qPrintable(a)); fflush(stdout); }
    static void print(const char *fmt, const QString &t, const QString &app, int s, int b,
                      uint id, const QString &hints)
    {
        printf(fmt, qPrintable(t), qPrintable(app), s, b, id, qPrintable(hints));
        fflush(stdout);
    }

    bool m_appsOnly;
    QSet<QString> m_seen;
};

volatile sig_atomic_t gStop = 0;

void onSignal(int)
{
    gStop = 1; // polled by a timer in main()
}

} // namespace

#include "main.moc"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    bool appsOnly = false;
    bool debug = false;
    int seconds = 0;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args.at(i) == QLatin1String("--apps"))
            appsOnly = true;
        else if (args.at(i) == QLatin1String("--debug"))
            debug = true;
        else if (args.at(i) == QLatin1String("--seconds") && i + 1 < args.size())
            seconds = args.at(++i).toInt();
        else {
            fprintf(stderr, "usage: notifmonitor-cli [--apps] [--seconds N] [--debug]\n");
            return 2;
        }
    }

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    Printer printer(appsOnly);
    NotifMonitor monitor;
    monitor.setDebugLogging(debug);
    QObject::connect(&monitor, SIGNAL(notificationReceived(notiffilter::IncomingNotification)),
                     &printer, SLOT(onNotification(notiffilter::IncomingNotification)));
    QObject::connect(&monitor, SIGNAL(stateChanged(int,QString)),
                     &printer, SLOT(onState(int,QString)));
    QTimer stopPoll;
    QObject::connect(&stopPoll, &QTimer::timeout, [&app]() { if (gStop) app.quit(); });
    stopPoll.start(500);
    if (seconds > 0)
        QTimer::singleShot(seconds * 1000, &app, SLOT(quit()));

    monitor.start();
    if (monitor.state() == NotifMonitor::CannotMonitor)
        return 1;
    return app.exec();
}
