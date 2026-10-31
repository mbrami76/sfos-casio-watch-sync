// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "servicebus.h"

#include "servicelog.h"

#include <QCoreApplication>
#include <QDBusMetaType>
#include <QProcess>

namespace service {

const char kBusName[] = "it.michelebrami.sfos_casio_sync";
const char kObjectPath[] = "/it/michelebrami/sfos_casio_sync";
const char kInterfaceName[] = "it.michelebrami.sfos_casio_sync";

bool registerOnBus(const QDBusConnection &bus, ServiceCore *core, ServiceObject *holder)
{
    qDBusRegisterMetaType<QList<QVariantMap> >();
    QDBusConnection b = bus;
    if (!b.isConnected())
        return false;
    new ServiceAdaptor(core, holder);
    if (!b.registerObject(QLatin1String(kObjectPath), holder))
        return false;
    // Not queued: if another instance owns the name, we exit.
    return b.registerService(QLatin1String(kBusName));
}

ServiceAdaptor::ServiceAdaptor(ServiceCore *core, ServiceObject *holder)
    : QDBusAbstractAdaptor(holder), m_core(core), m_holder(holder)
{
    connect(core, SIGNAL(statusChanged()), this, SIGNAL(StatusChanged()));
    connect(core, SIGNAL(watchListReady(quint32, QList<QVariantMap>, QString)), this,
            SLOT(onListReady(quint32, QList<QVariantMap>, QString)));
}

QVariantMap ServiceAdaptor::GetStatus()
{
    return m_core->status();
}

bool ServiceAdaptor::SyncNow(const QString &key)
{
    return m_core->syncNow(key);
}

void ServiceAdaptor::ReloadSettings()
{
    m_core->reloadSettings();
}

bool ServiceAdaptor::TestNotification()
{
    return m_core->testNotification();
}

QList<QVariantMap> ServiceAdaptor::ListWatches()
{
    if (!m_holder->inCall())
        return QList<QVariantMap>();
    m_holder->delayReply();
    Waiting w;
    w.message = m_holder->callMessage();
    w.connection = m_holder->callConnection();
    m_waiting.insert(m_core->requestWatchList(), w);
    return QList<QVariantMap>();
}

void ServiceAdaptor::onListReady(quint32 ticket, const QList<QVariantMap> &watches,
                                 const QString &error)
{
    if (!m_waiting.contains(ticket))
        return;
    const Waiting w = m_waiting.take(ticket);
    if (!error.isEmpty() && watches.isEmpty()) {
        w.connection.send(w.message.createErrorReply(
            QStringLiteral("it.michelebrami.sfos_casio_sync.Error.Bluetooth"), error));
        return;
    }
    QDBusMessage reply = w.message.createReply();
    QList<QVariant> args;
    args << QVariant::fromValue(watches);
    reply.setArguments(args);
    w.connection.send(reply);
}

void ServiceAdaptor::OpenApp()
{
    debugLog(QStringLiteral("OpenApp"));
    QProcess::startDetached(QCoreApplication::applicationFilePath(), QStringList());
}

} // namespace service
