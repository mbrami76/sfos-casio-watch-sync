// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "serviceclient.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QProcess>

namespace uibridge {

const char kServiceName[] = "it.michelebrami.sfos_casio_sync";
const char kServicePath[] = "/it/michelebrami/sfos_casio_sync";
const char kServiceInterface[] = "it.michelebrami.sfos_casio_sync";

static const int kCallTimeoutMs = 5000;
static const int kListTimeoutMs = 30000;    // answered after a running session

QList<QVariantMap> toMapList(const QVariant &v)
{
    QList<QVariantMap> out;
    if (v.userType() == qMetaTypeId<QDBusArgument>())
        return qdbus_cast<QList<QVariantMap> >(v.value<QDBusArgument>());
    if (v.userType() == qMetaTypeId<QList<QVariantMap> >())
        return v.value<QList<QVariantMap> >();
    if (v.canConvert<QVariantList>()) {
        const QVariantList l = v.toList();
        for (int i = 0; i < l.size(); ++i) {
            if (l.at(i).userType() == qMetaTypeId<QDBusArgument>()) {
                QVariantMap m;
                l.at(i).value<QDBusArgument>() >> m;
                out.append(m);
            } else {
                out.append(l.at(i).toMap());
            }
        }
    }
    return out;
}

QStringList toStringList(const QVariant &v)
{
    if (v.userType() == qMetaTypeId<QDBusArgument>())
        return qdbus_cast<QStringList>(v.value<QDBusArgument>());
    return v.toStringList();
}

ServiceStatus parseStatus(const QVariantMap &raw)
{
    ServiceStatus s;
    if (!raw.contains(QStringLiteral("serviceState")))
        return s;
    s.valid = true;
    s.serviceState = raw.value(QStringLiteral("serviceState")).toString();
    s.busy = raw.value(QStringLiteral("busy")).toBool();
    s.masterOn = raw.value(QStringLiteral("masterOn")).toBool();
    s.intervalMinutes = raw.value(QStringLiteral("intervalMinutes")).toInt();
    s.modelTableLoaded = raw.value(QStringLiteral("modelTableLoaded")).toBool();
    s.monitorState = raw.value(QStringLiteral("monitorState")).toString();
    s.monitorReason = raw.value(QStringLiteral("monitorReason")).toString();
    s.riskProblems = toStringList(raw.value(QStringLiteral("riskProblems")));
    s.watches = toMapList(raw.value(QStringLiteral("watches")));
    return s;
}

ServiceClient::ServiceClient(const QDBusConnection &bus, QObject *parent)
    : QObject(parent), m_bus(bus), m_watcher(QLatin1String(kServiceName), m_bus,
                                             QDBusServiceWatcher::WatchForOwnerChange),
      m_available(false), m_statusInFlight(false), m_statusAgain(false), m_listing(false)
{
    qDBusRegisterMetaType<QList<QVariantMap> >();
    connect(&m_watcher, SIGNAL(serviceOwnerChanged(QString, QString, QString)), this,
            SLOT(onOwnerChanged(QString, QString, QString)));
    m_bus.connect(QLatin1String(kServiceName), QLatin1String(kServicePath),
                  QLatin1String(kServiceInterface), QStringLiteral("StatusChanged"), this,
                  SLOT(onStatusChangedSignal()));
    // NameHasOwner does not start the service.
    if (m_bus.isConnected() && m_bus.interface()
        && m_bus.interface()->isServiceRegistered(QLatin1String(kServiceName)).value())
        m_available = true;
}

void ServiceClient::setAvailable(bool on)
{
    if (m_available == on)
        return;
    m_available = on;
    emit availableChanged();
}

void ServiceClient::onOwnerChanged(const QString &, const QString &, const QString &newOwner)
{
    if (newOwner.isEmpty()) {
        setAvailable(false);
        m_status = ServiceStatus();
        m_listing = false;
        emit listingChanged();
        emit statusChanged();
    } else {
        setAvailable(true);
        refresh();
    }
}

void ServiceClient::onStatusChangedSignal()
{
    refresh();
}

void ServiceClient::refresh()
{
    if (!m_available)
        return;
    if (m_statusInFlight) {
        m_statusAgain = true;
        return;
    }
    m_statusInFlight = true;
    QDBusMessage m = QDBusMessage::createMethodCall(
        QLatin1String(kServiceName), QLatin1String(kServicePath),
        QLatin1String(kServiceInterface), QStringLiteral("GetStatus"));
    QDBusPendingCallWatcher *w = new QDBusPendingCallWatcher(m_bus.asyncCall(m, kCallTimeoutMs), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *cw) {
        QDBusPendingReply<QVariantMap> reply = *cw;
        cw->deleteLater();
        m_statusInFlight = false;
        if (reply.isValid())
            m_status = parseStatus(reply.value());
        else
            m_status = ServiceStatus();
        emit statusChanged();
        if (m_statusAgain) {
            m_statusAgain = false;
            refresh();
        }
    });
}

void ServiceClient::listWatches()
{
    if (!m_available || m_listing)
        return;
    m_listing = true;
    m_listError.clear();
    emit listingChanged();
    QDBusMessage m = QDBusMessage::createMethodCall(
        QLatin1String(kServiceName), QLatin1String(kServicePath),
        QLatin1String(kServiceInterface), QStringLiteral("ListWatches"));
    QDBusPendingCallWatcher *w = new QDBusPendingCallWatcher(m_bus.asyncCall(m, kListTimeoutMs), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *cw) {
        QDBusPendingReply<QList<QVariantMap> > reply = *cw;
        cw->deleteLater();
        m_listing = false;
        if (reply.isValid()) {
            m_listed = reply.value();
            m_listError.clear();
        } else {
            m_listError = reply.error().message();
            if (m_listError.isEmpty())
                m_listError = reply.error().name();
        }
        emit listingChanged();
    });
}

void ServiceClient::syncNow(const QString &key)
{
    if (!m_available) {
        emit syncRequested(false);
        return;
    }
    QDBusMessage m = QDBusMessage::createMethodCall(
        QLatin1String(kServiceName), QLatin1String(kServicePath),
        QLatin1String(kServiceInterface), QStringLiteral("SyncNow"));
    m << key;
    QDBusPendingCallWatcher *w = new QDBusPendingCallWatcher(m_bus.asyncCall(m, kCallTimeoutMs), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *cw) {
        QDBusPendingReply<bool> reply = *cw;
        cw->deleteLater();
        emit syncRequested(reply.isValid() && reply.value());
    });
}

void ServiceClient::testNotification()
{
    if (!m_available) {
        emit testNotificationResult(false);
        return;
    }
    QDBusMessage m = QDBusMessage::createMethodCall(
        QLatin1String(kServiceName), QLatin1String(kServicePath),
        QLatin1String(kServiceInterface), QStringLiteral("TestNotification"));
    QDBusPendingCallWatcher *w = new QDBusPendingCallWatcher(m_bus.asyncCall(m, kCallTimeoutMs), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *cw) {
        QDBusPendingReply<bool> reply = *cw;
        cw->deleteLater();
        emit testNotificationResult(reply.isValid() && reply.value());
    });
}

void ServiceClient::reloadSettings()
{
    if (!m_available)
        return;     // the service reads the file when it starts
    QDBusMessage m = QDBusMessage::createMethodCall(
        QLatin1String(kServiceName), QLatin1String(kServicePath),
        QLatin1String(kServiceInterface), QStringLiteral("ReloadSettings"));
    QDBusPendingCallWatcher *w = new QDBusPendingCallWatcher(m_bus.asyncCall(m, kCallTimeoutMs), this);
    connect(w, &QDBusPendingCallWatcher::finished, w, &QObject::deleteLater);
}

void ServiceClient::startService()
{
    if (m_available)
        return;
    QDBusMessage m = QDBusMessage::createMethodCall(
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
        QStringLiteral("org.freedesktop.DBus"), QStringLiteral("StartServiceByName"));
    m << QString::fromLatin1(kServiceName) << quint32(0);
    QDBusPendingCallWatcher *w = new QDBusPendingCallWatcher(m_bus.asyncCall(m, 10000), this);
    connect(w, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *cw) {
        QDBusPendingReply<quint32> reply = *cw;
        cw->deleteLater();
        // No activation file (a development install): start this binary as the service.
        if (!reply.isValid() && !m_available)
            QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                    QStringList() << QStringLiteral("--service"));
    });
}

} // namespace uibridge
