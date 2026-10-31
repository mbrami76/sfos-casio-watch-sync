// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Client of the service's D-Bus API ("D-Bus API (final)"). All calls are asynchronous. The service
// may be absent: that is a state (`available` false), not an error. The UI never talks to a watch;
// everything about a watch goes through these calls.
#ifndef SERVICECLIENT_H
#define SERVICECLIENT_H

#include <QDBusConnection>
#include <QDBusServiceWatcher>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace uibridge {

// The names of the service (the same as service/servicebus.cpp; the bridge
// does not link the service library).
extern const char kServiceName[];       // it.michelebrami.sfos_casio_sync
extern const char kServicePath[];       // /it/michelebrami/sfos_casio_sync
extern const char kServiceInterface[];  // it.michelebrami.sfos_casio_sync

// GetStatus, mapped.
struct ServiceStatus {
    bool valid;                     // a reply with serviceState was understood
    QString serviceState;
    bool busy;
    bool masterOn;
    int intervalMinutes;
    bool modelTableLoaded;
    QString monitorState;           // off | running | not-running | cannot-monitor
    QString monitorReason;
    QStringList riskProblems;
    QList<QVariantMap> watches;     // one map per entry of the settings list, in order
    ServiceStatus() : valid(false), busy(false), masterOn(false), intervalMinutes(0),
                      modelTableLoaded(false) {}
};

// A nested a{sv} / aa{sv} value arrives as QDBusArgument over the bus and as a
// plain list in tests; both are accepted.
QList<QVariantMap> toMapList(const QVariant &v);
QStringList toStringList(const QVariant &v);
ServiceStatus parseStatus(const QVariantMap &raw);

class ServiceClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool statusKnown READ statusKnown NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY statusChanged)
    Q_PROPERTY(QString monitorState READ monitorState NOTIFY statusChanged)
    Q_PROPERTY(QString monitorReason READ monitorReason NOTIFY statusChanged)
    Q_PROPERTY(bool listing READ listing NOTIFY listingChanged)
    Q_PROPERTY(QString listError READ listError NOTIFY listingChanged)
public:
    explicit ServiceClient(const QDBusConnection &bus = QDBusConnection::sessionBus(),
                           QObject *parent = 0);

    bool available() const { return m_available; }          // the name is owned on the bus
    bool statusKnown() const { return m_status.valid; }     // GetStatus answered
    bool busy() const { return m_status.valid && m_status.busy; }
    QString monitorState() const { return m_status.monitorState; }
    QString monitorReason() const { return m_status.monitorReason; }
    bool listing() const { return m_listing; }
    QString listError() const { return m_listError; }
    const ServiceStatus &status() const { return m_status; }
    QList<QVariantMap> listedWatches() const { return m_listed; }

public slots:
    void refresh();                          // GetStatus
    void listWatches();                      // ListWatches (can take long: waits for a session)
    void syncNow(const QString &key);        // empty key = every usable watch
    void syncAll() { syncNow(QString()); }
    void testNotification();
    void reloadSettings();
    // Asks the bus to start the service (D-Bus activation); without an
    // activation file it starts this binary with --service.
    void startService();

signals:
    void availableChanged();
    void statusChanged();                    // GetStatus answered or the service went away
    void listingChanged();                   // ListWatches started, answered or failed
    void syncRequested(bool accepted);       // answer of SyncNow
    void testNotificationResult(bool sent);

private slots:
    void onOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void onStatusChangedSignal();

private:
    void setAvailable(bool on);

    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    bool m_available;
    ServiceStatus m_status;
    bool m_statusInFlight;
    bool m_statusAgain;
    bool m_listing;
    QString m_listError;
    QList<QVariantMap> m_listed;
};

} // namespace uibridge

#endif // SERVICECLIENT_H
