// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "bluezadapter.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

typedef QMap<QString, QVariantMap> BluezInterfaceMap;
typedef QMap<QDBusObjectPath, BluezInterfaceMap> BluezObjectMap;
Q_DECLARE_METATYPE(BluezInterfaceMap)
Q_DECLARE_METATYPE(BluezObjectMap)

namespace watchlink {

namespace {

const char kService[] = "org.bluez";
const char kDevice1[] = "org.bluez.Device1";
const char kChar1[] = "org.bluez.GattCharacteristic1";
const char kProps[] = "org.freedesktop.DBus.Properties";

const int kCallTimeoutMs = 10000;

QDBusConnection bus() { return QDBusConnection::systemBus(); }

QDBusMessage method(const QString &path, const char *iface, const char *name)
{
    return QDBusMessage::createMethodCall(QLatin1String(kService), path, QLatin1String(iface),
                                          QLatin1String(name));
}

// Blocking call; false with a short text on a D-Bus error.
bool callBlocking(const QDBusMessage &msg, QString *error, int timeoutMs = kCallTimeoutMs)
{
    const QDBusMessage reply = bus().call(msg, QDBus::Block, timeoutMs);
    if (reply.type() == QDBusMessage::ReplyMessage)
        return true;
    if (error)
        *error = reply.errorMessage().isEmpty() ? reply.errorName() : reply.errorMessage();
    return false;
}

bool managedObjects(BluezObjectMap *out, QString *error)
{
    QDBusMessage m = QDBusMessage::createMethodCall(
        QLatin1String(kService), QStringLiteral("/"),
        QStringLiteral("org.freedesktop.DBus.ObjectManager"), QStringLiteral("GetManagedObjects"));
    const QDBusMessage reply = bus().call(m, QDBus::Block, kCallTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage) {
        if (error)
            *error = reply.errorMessage().isEmpty() ? reply.errorName() : reply.errorMessage();
        return false;
    }
    QDBusReply<BluezObjectMap> r(reply);
    if (!r.isValid()) {
        if (error)
            *error = QStringLiteral("unexpected reply of GetManagedObjects");
        return false;
    }
    *out = r.value();
    return true;
}

void pause(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, SLOT(quit()));
    loop.exec();
}

} // namespace

BluezDBusAdapter::BluezDBusAdapter(QObject *parent)
    : QObject(parent), m_wantFeature(0), m_gotAnswer(false), m_loop(0)
{
    qDBusRegisterMetaType<BluezInterfaceMap>();
    qDBusRegisterMetaType<BluezObjectMap>();
}

bool BluezDBusAdapter::devices(QList<BluezDevice> *out, QString *error)
{
    BluezObjectMap objs;
    if (!managedObjects(&objs, error))
        return false;
    out->clear();
    for (BluezObjectMap::const_iterator it = objs.constBegin(); it != objs.constEnd(); ++it) {
        const BluezInterfaceMap::const_iterator d = it.value().constFind(QLatin1String(kDevice1));
        if (d == it.value().constEnd())
            continue;
        BluezDevice dev;
        dev.path = it.key().path();
        dev.address = d.value().value(QStringLiteral("Address")).toString();
        dev.name = d.value().value(QStringLiteral("Name")).toString();
        dev.alias = d.value().value(QStringLiteral("Alias")).toString();
        dev.paired = d.value().value(QStringLiteral("Paired")).toBool();
        dev.connected = d.value().value(QStringLiteral("Connected")).toBool();
        out->append(dev);
    }
    return true;
}

bool BluezDBusAdapter::deviceProperty(const QString &path, const char *name, QVariant *value,
                                      QString *error)
{
    QDBusMessage m = method(path, kProps, "Get");
    m << QString::fromLatin1(kDevice1) << QString::fromLatin1(name);
    const QDBusMessage reply = bus().call(m, QDBus::Block, kCallTimeoutMs);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        if (error)
            *error = reply.errorMessage();
        return false;
    }
    *value = reply.arguments().first().value<QDBusVariant>().variant();
    return true;
}

bool BluezDBusAdapter::connectDevice(const QString &path, int timeoutMs, QString *error)
{
    m_charCache.clear();
    QDBusPendingCall call = bus().asyncCall(method(path, kDevice1, "Connect"), timeoutMs + 5000);
    QDBusPendingCallWatcher watcher(call);
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&watcher, SIGNAL(finished(QDBusPendingCallWatcher*)), &loop, SLOT(quit()));
    connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));
    timer.start(timeoutMs);
    if (!watcher.isFinished())
        loop.exec();
    if (!watcher.isFinished()) {
        // BlueZ keeps a Connect running after our timeout: cancel it.
        disconnectDevice(path);
        if (error)
            *error = tr("connect timeout after %1 s").arg(timeoutMs / 1000);
        return false;
    }
    QDBusPendingReply<> reply = watcher;
    if (reply.isError()) {
        if (error)
            *error = reply.error().message().isEmpty() ? reply.error().name()
                                                       : reply.error().message();
        return false;
    }
    return true;
}

bool BluezDBusAdapter::waitServicesResolved(const QString &path, int timeoutMs, QString *error)
{
    QElapsedTimer t;
    t.start();
    while (true) {
        QVariant v;
        QString err;
        if (!deviceProperty(path, "ServicesResolved", &v, &err)) {
            if (error)
                *error = err;
            return false;
        }
        if (v.toBool())
            return true;
        if (t.elapsed() >= timeoutMs) {
            if (error)
                *error = tr("timeout after %1 s").arg(timeoutMs / 1000);
            return false;
        }
        pause(100);
    }
}

QString BluezDBusAdapter::characteristicPath(const QString &devicePath, const QString &uuid,
                                             QString *error)
{
    const QString cacheKey = devicePath + QLatin1Char('|') + uuid.toLower();
    if (m_charCache.contains(cacheKey))
        return m_charCache.value(cacheKey);
    BluezObjectMap objs;
    if (!managedObjects(&objs, error))
        return QString();
    const QString prefix = devicePath + QLatin1Char('/');
    for (BluezObjectMap::const_iterator it = objs.constBegin(); it != objs.constEnd(); ++it) {
        if (!it.key().path().startsWith(prefix))
            continue;
        const BluezInterfaceMap::const_iterator c = it.value().constFind(QLatin1String(kChar1));
        if (c == it.value().constEnd())
            continue;
        if (c.value().value(QStringLiteral("UUID")).toString().toLower() == uuid.toLower()) {
            m_charCache.insert(cacheKey, it.key().path());
            return it.key().path();
        }
    }
    if (error)
        *error = tr("characteristic %1 not found").arg(uuid.left(8));
    return QString();
}

bool BluezDBusAdapter::startNotify(const QString &path, const QString &uuid, QString *error)
{
    const QString c = characteristicPath(path, uuid, error);
    if (c.isEmpty())
        return false;
    QString err;
    if (callBlocking(method(c, kChar1, "StartNotify"), &err))
        return true;
    if (err.contains(QLatin1String("lready"), Qt::CaseInsensitive))   // already notifying
        return true;
    if (error)
        *error = err;
    return false;
}

bool BluezDBusAdapter::write(const QString &path, const QString &uuid, const QByteArray &value,
                             bool withResponse, QString *error)
{
    const QString c = characteristicPath(path, uuid, error);
    if (c.isEmpty())
        return false;
    QVariantMap options;
    options.insert(QStringLiteral("type"),
                   withResponse ? QStringLiteral("request") : QStringLiteral("command"));
    QDBusMessage m = method(c, kChar1, "WriteValue");
    m << QVariant(value) << QVariant(options);
    return callBlocking(m, error);
}

bool BluezDBusAdapter::readFeature(const QString &path, quint8 feature, int timeoutMs,
                                   QByteArray *answer, QString *error)
{
    const QString data = characteristicPath(path, QLatin1String(kUuidData), error);
    if (data.isEmpty())
        return false;
    m_wantFeature = feature;
    m_gotAnswer = false;
    m_answer.clear();
    QEventLoop loop;
    m_loop = &loop;
    bus().connect(QLatin1String(kService), data, QLatin1String(kProps),
                  QStringLiteral("PropertiesChanged"), this,
                  SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));

    QByteArray request(1, static_cast<char>(feature));
    bool ok = write(path, QLatin1String(kUuidRequest), request, false, error);
    if (ok) {
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, SIGNAL(timeout()), &loop, SLOT(quit()));
        timer.start(timeoutMs);
        if (!m_gotAnswer)
            loop.exec();
        if (m_gotAnswer) {
            *answer = m_answer;
        } else {
            ok = false;
            if (error)
                *error = tr("no answer to feature 0x%1").arg(feature, 2, 16, QLatin1Char('0'));
        }
    }
    bus().disconnect(QLatin1String(kService), data, QLatin1String(kProps),
                     QStringLiteral("PropertiesChanged"), this,
                     SLOT(onPropertiesChanged(QString, QVariantMap, QStringList)));
    m_loop = 0;
    return ok;
}

void BluezDBusAdapter::onPropertiesChanged(const QString &iface, const QVariantMap &changed,
                                           const QStringList &)
{
    if (!m_loop || iface != QLatin1String(kChar1) || !changed.contains(QStringLiteral("Value")))
        return;
    const QVariant v = changed.value(QStringLiteral("Value"));
    QByteArray bytes;
    if (v.userType() == qMetaTypeId<QDBusArgument>())
        bytes = qdbus_cast<QByteArray>(v.value<QDBusArgument>());
    else
        bytes = v.toByteArray();
    if (!bytes.isEmpty() && static_cast<quint8>(bytes.at(0)) == m_wantFeature && !m_gotAnswer) {
        m_answer = bytes;
        m_gotAnswer = true;
        m_loop->quit();
    }
}

void BluezDBusAdapter::disconnectDevice(const QString &path)
{
    callBlocking(method(path, kDevice1, "Disconnect"), 0, 5000);
    m_charCache.clear();
}

void BluezDBusAdapter::settle(int ms)
{
    pause(ms);
}

} // namespace watchlink
