// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// service: the seams between the wiring logic (servicecore.h) and everything with I/O or threads.
// The real implementations are in workerrunner.h, monitorsource.h and nemopublisher.h; the tests
// use fakes. Requirements: R-GEN-5, R-NOTIF-4. (The stay-awake hold is part of
// WakeScheduler,.)
#ifndef SERVICEIFACE_H
#define SERVICEIFACE_H

#include "notiffilter.h"
#include "settings.h"
#include "watchlink.h"

#include <QList>
#include <QMetaType>
#include <QObject>
#include <QString>

namespace service {

// A paired device that matches the model table, as the BlueZ side saw it.
struct FoundDevice {
    QString address;
    QString name;        // Device1.Name
    QString alias;       // Device1.Alias (BlueZ's, not the app's alias)
    QString modelId;
    QString modelLabel;
    bool supported;      // model status "supported" (tested on hardware)
    bool connected;
    FoundDevice() : supported(false), connected(false) {}
};

// One unit of work for the worker: queue the alerts for `alertTargets`, then
// run the sessions in list order. The service has only one job in flight.
struct SessionJob {
    QList<notiffilter::WatchNotification> alerts;
    QList<settings::WatchEntry> alertTargets;
    QList<watchlink::SessionRequest> requests;
    QStringList clearKeys;    // queues to empty first (master switch turned off)
};

// Runs the blocking BLE sessions off the main thread. One call at a time: the service never submits
// while a job is in flight.
class SessionRunner : public QObject
{
    Q_OBJECT
public:
    explicit SessionRunner(QObject *parent = 0) : QObject(parent) {}
    virtual void submit(const SessionJob &job) = 0;
    virtual void listWatches() = 0;
signals:
    void jobFinished(const QList<watchlink::SessionResult> &results);
    // `error` is empty on success.
    void watchesListed(const QList<service::FoundDevice> &devices, const QString &error);
};

// The notification monitor. State values are notifmonitor::NotifMonitor::State.
class NotifSource : public QObject
{
    Q_OBJECT
public:
    explicit NotifSource(QObject *parent = 0) : QObject(parent) {}
    virtual void start() = 0;      // safe when already running
    virtual void stop() = 0;
    virtual int state() const = 0;
    virtual QString reason() const = 0;
    virtual void setDebugLogging(bool on) = 0;
signals:
    void notificationReceived(const notiffilter::IncomingNotification &n);
    void stateChanged(int state, const QString &reason);
};

// The resident notification on the phone.
class StatusPublisher
{
public:
    virtual ~StatusPublisher() {}
    virtual void publish(const QString &summary, const QString &body) = 0;
};

} // namespace service

Q_DECLARE_METATYPE(service::SessionJob)
Q_DECLARE_METATYPE(QList<service::FoundDevice>)
Q_DECLARE_METATYPE(QList<watchlink::SessionResult>)

#endif // SERVICEIFACE_H
