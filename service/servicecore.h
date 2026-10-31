// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// servicecore: wires the modules of the headless service. No I/O of its own: settings, state, the
// BLE worker, the notification monitor, the wake-ups (with the "stay awake" hold) and the resident
// notification are injected, so the logic is tested on the PC with fakes. Lives in the main thread;
// the blocking sessions run in the worker behind SessionRunner. Requirements: R-GEN-5, R-TIME-8,
// R-TIME-9, R-NOTIF-1, R-NOTIF-4, R-NOTIF-10, R-NOTIF-11.
#ifndef SERVICECORE_H
#define SERVICECORE_H

#include "scheduler.h"
#include "serviceiface.h"
#include "settings.h"
#include "wakescheduler.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

namespace service {

struct ServiceDeps {
    settings::Settings *settings;
    settings::State *state;
    const watchproto::ModelTable *models;
    const scheduler::ClockSource *clock;
    scheduler::WakeScheduler *wake;
    SessionRunner *runner;
    NotifSource *monitor;
    StatusPublisher *publisher;
    ServiceDeps()
        : settings(0), state(0), models(0), clock(0), wake(0), runner(0), monitor(0),
          publisher(0) {}
};

class ServiceCore : public QObject
{
    Q_OBJECT
public:
    // Nothing is owned. All pointers must outlive the object.
    explicit ServiceCore(const ServiceDeps &deps, QObject *parent = 0);

    // Loads the state, starts the monitor if the master switch is on, decides
    // what is due, publishes the resident notification and requests the wake-up.
    void start();

    // ---- what the D-Bus API calls
    // Status: per watch of the settings list, and global fields.
    QVariantMap status() const;
    // Empty key = every usable watch. False if the key is not a usable watch.
    bool syncNow(const QString &key);
    void reloadSettings();
    // Fixed text to every usable watch, whatever the master switch says. False
    // if there is no usable watch.
    bool testNotification();
    // Asynchronous: the answer comes with watchListReady(ticket, ...).
    quint32 requestWatchList();

    // A watch is used for syncs and alerts only if it is enabled and its model
    // is known and either supported or acknowledged by the user (the
    // `riskAcknowledged` rule). `why` gets the reason when it returns false.
    bool usable(const settings::WatchEntry &w, QString *why = 0) const;

    bool busy() const { return m_running; }

signals:
    void statusChanged();
    // `watches` holds the same maps that ListWatches returns.
    void watchListReady(quint32 ticket, const QList<QVariantMap> &watches, const QString &error);

private slots:
    void onWake();
    void onSettingsChanged();
    void onNotification(const notiffilter::IncomingNotification &n);
    void onMonitorState();
    void onJobFinished(const QList<watchlink::SessionResult> &results);
    void onWatchesListed(const QList<service::FoundDevice> &devices, const QString &error);
    void onPumpNow();

private:
    struct PendingAlert {
        notiffilter::WatchNotification n;
        QDateTime received;
    };

    const watchproto::ModelInfo *modelFor(const settings::WatchEntry &w) const;
    QString defaultSilhouetteOf(const QString &modelId) const;
    QList<scheduler::WatchRunState> runStates() const;
    notiffilter::NotifSettings notifSettings() const;
    void applyMaster();
    void evaluate();
    void pump();
    void rescheduleWake();
    void startRun();
    void endRun();
    void queueAlert(const notiffilter::WatchNotification &n);
    void updateResident();
    QString residentBody() const;
    QString formatTime(const QDateTime &t) const;
    QVariantMap watchStatus(const settings::WatchEntry &w) const;

    settings::Settings *m_settings;
    settings::State *m_state;
    const watchproto::ModelTable *m_models;
    const scheduler::ClockSource *m_clock;
    scheduler::WakeScheduler *m_wake;
    SessionRunner *m_runner;
    NotifSource *m_monitor;
    StatusPublisher *m_publisher;

    bool m_started;
    bool m_selfChange;               // our own writes to the settings
    bool m_running;                  // a job or a list request is in flight
    quint32 m_listing;               // ticket of the list in flight, 0 if none
    quint32 m_nextTicket;
    QList<quint32> m_listTickets;    // waiting
    QList<PendingAlert> m_pending;   // passed the filter, waiting for the worker
    QSet<QString> m_syncKeys;        // watches that want a time sync
    QMap<QString, bool> m_inflight;  // key -> time sync requested, for the job in flight
    QMap<QString, QDateTime> m_retryAt;   // alerts kept in the worker (WatchLink::nextAttempt)
    bool m_clearQueues;
    bool m_masterWas;
    QString m_lastResident;
    bool m_residentPublished;
};

} // namespace service

#endif // SERVICECORE_H
