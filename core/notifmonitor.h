// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// notifmonitor: listens to every org.freedesktop.Notifications.Notify call on the session bus and
// hands each one on as an IncomingNotification. It runs on its own bus connection, turned into a
// monitor with BecomeMonitor, and never sends a message on it. Requirements: R-NOTIF-2, R-NOTIF-3,
// R-NOTIF-4, R-PLAT-1, R-PLAT-6.
//
// Why libdbus and not QtDBus for the monitor connection: a monitor must not send anything, not even
// the error reply a D-Bus library gives by default to a method call nobody handles (the bus then
// closes the connection). QtDBus has no public way to swallow such calls; with libdbus the
// connection has one filter that reports every message as handled.
//
// No notification text is logged or put into an error string. Debug logging, when enabled, records
// time, app_name and lengths.
#ifndef NOTIFMONITOR_H
#define NOTIFMONITOR_H

#include "notiffilter.h"

#include <QByteArray>
#include <QDateTime>
#include <QElapsedTimer>
#include <QMetaType>
#include <QObject>
#include <QSocketNotifier>
#include <QString>
#include <QTimer>
#include <QVariantList>

struct DBusConnection;
struct DBusMessage;

Q_DECLARE_METATYPE(notiffilter::IncomingNotification)

namespace notifmonitor {

// app_name of this application's own notifications (the resident status
// notification and the test notification). `service` publishes with this
// name and passes it as NotifSettings::ownAppName so notiffilter drops them.
// [assumption] the official title; to be confirmed when `service` is written.
QString ownAppName();

// Retry delays after the monitor connection is lost or cannot be made:
// 1 s, 2 s, 4 s, ... doubling, at most 1 min. `attempt` counts from 0.
const int kRetryInitialMs = 1000;
const int kRetryMaxMs = 60 * 1000;
int retryDelayMs(int attempt);

// Pure: the eight arguments of Notify(app_name, replaces_id, app_icon,
// summary, body, actions, hints, expire_timeout) as received -> the output
// type. Never throws; a missing or wrongly typed argument gives an empty
// string, 0 or an empty map. Text is passed through unchanged (no trimming,
// no truncation: that is for notiffilter and watchproto). The hints are kept
// as they are, a QVariantMap of name -> value, not interpreted. `when` is
// stored as given (the phone time at reception).
notiffilter::IncomingNotification parseNotify(const QVariantList &args,
                                              const QDateTime &when);

class NotifMonitor : public QObject
{
    Q_OBJECT
public:
    enum State {
        NotRunning,     // not started, stopped, or connection lost (retrying if started)
        Running,        // the connection is a monitor
        CannotMonitor   // the bus refuses monitoring (sandbox, policy); not retried
    };

    // busAddress empty = the session bus; otherwise a D-Bus address (tests).
    explicit NotifMonitor(const QByteArray &busAddress = QByteArray(),
                          QObject *parent = 0);
    ~NotifMonitor();

    // Connect and call BecomeMonitor. Blocks for the bus answer, 5 s at most.
    // On a transient failure (no bus, connection lost) it retries by itself
    // with retryDelayMs(); on a refusal the state becomes CannotMonitor.
    // Calling it again while running does nothing; after CannotMonitor it
    // tries once more.
    void start();
    void stop();

    State state() const { return m_state; }
    // Short, translated, never contains notification text; empty when Running.
    QString reason() const { return m_reason; }

    // Debug log (qDebug) with metadata only; default off.
    void setDebugLogging(bool on) { m_debug = on; }

signals:
    // One per Notify call, whatever replaces_id says.
    void notificationReceived(const notiffilter::IncomingNotification &n);
    void stateChanged(int state, const QString &reason);

private slots:
    void onSocketActivity();
    void handleLost();
    void retryNow();

private:
    friend struct MonitorGlue;

    void connectNow();
    void teardown();
    void scheduleRetry();
    void setState(State s, const QString &reason);
    void pump();
    void onNotify(DBusMessage *msg);

    QByteArray m_busAddress;
    DBusConnection *m_conn;
    QSocketNotifier *m_notifier;
    QTimer m_retryTimer;
    QElapsedTimer m_upTime;
    State m_state;
    QString m_reason;
    bool m_wanted;
    bool m_lostQueued;
    bool m_debug;
    int m_failures;
};

} // namespace notifmonitor

#endif // NOTIFMONITOR_H
