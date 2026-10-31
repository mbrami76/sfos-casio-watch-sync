// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// D-Bus API of the service on the session bus. Names: bus name
// `it.michelebrami.sfos_casio_sync` (same underscore form as the interface; hyphens are not allowed
// in interface or object path elements, so all names use underscores).
#ifndef SERVICEBUS_H
#define SERVICEBUS_H

#include "servicecore.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QList>
#include <QMap>
#include <QVariantMap>

namespace service {

extern const char kBusName[];        // it.michelebrami.sfos_casio_sync
extern const char kObjectPath[];     // /it/michelebrami/sfos_casio_sync
extern const char kInterfaceName[];  // it.michelebrami.sfos_casio_sync

// Registers the object and the bus name on `bus`. False if the name is taken
// (another instance runs) or the object cannot be registered. Call it before
// ServiceCore::start(), so the second instance exits before it does anything.
// The exported object. QtDBus sets the call context on the exported object,
// not on its adaptor, so the delayed reply of ListWatches goes through it.
class ServiceObject : public QObject, protected QDBusContext
{
    Q_OBJECT
public:
    explicit ServiceObject(QObject *parent = 0) : QObject(parent) {}
    bool inCall() const { return calledFromDBus(); }
    QDBusMessage callMessage() const { return message(); }
    QDBusConnection callConnection() const { return connection(); }
    void delayReply() { setDelayedReply(true); }
};

bool registerOnBus(const QDBusConnection &bus, ServiceCore *core, ServiceObject *holder);

class ServiceAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "it.michelebrami.sfos_casio_sync")
public:
    ServiceAdaptor(ServiceCore *core, ServiceObject *holder);

public slots:
    QVariantMap GetStatus();
    // Empty key = every usable watch. True if at least one session was requested.
    bool SyncNow(const QString &key);
    void ReloadSettings();
    // True if there is a usable watch to send it to.
    bool TestNotification();
    // Answered when the worker has looked at BlueZ (after a running session).
    QList<QVariantMap> ListWatches();
    // Starts the UI (the binary without --service). Used by the resident notification.
    void OpenApp();

signals:
    void StatusChanged();

private slots:
    void onListReady(quint32 ticket, const QList<QVariantMap> &watches, const QString &error);

private:
    struct Waiting {
        QDBusMessage message;
        QDBusConnection connection;
        Waiting() : connection(QString()) {}
    };
    ServiceCore *m_core;
    ServiceObject *m_holder;
    QMap<quint32, Waiting> m_waiting;
};

} // namespace service

#endif // SERVICEBUS_H
