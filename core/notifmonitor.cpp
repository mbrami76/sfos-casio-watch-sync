// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "notifmonitor.h"

#include <QDBusVariant>
#include <QDebug>
#include <QMap>
#include <QMetaObject>
#include <QStringList>

#include <dbus/dbus.h>

namespace notifmonitor {

QString ownAppName()
{
    return QStringLiteral("Casio Watch Sync");
}

int retryDelayMs(int attempt)
{
    if (attempt < 0)
        attempt = 0;
    if (attempt > 20)
        attempt = 20;
    int ms = kRetryInitialMs;
    for (int i = 0; i < attempt && ms < kRetryMaxMs; ++i)
        ms *= 2;
    return ms < kRetryMaxMs ? ms : kRetryMaxMs;
}

namespace {

const char kNotifInterface[] = "org.freedesktop.Notifications";
const char kNotifMember[] = "Notify";
const int kMaxDepth = 8;
const int kMaxArgs = 16;
const int kBecomeMonitorTimeoutMs = 5000;

QString stringAt(const QVariantList &args, int i)
{
    if (i >= args.size())
        return QString();
    const QVariant &v = args.at(i);
    return v.type() == QVariant::String ? v.toString() : QString();
}

uint replacesIdAt(const QVariantList &args, int i)
{
    if (i >= args.size())
        return 0;
    const QVariant &v = args.at(i);
    qulonglong u = 0;
    switch (v.userType()) {
    case QMetaType::ULongLong:
        u = v.toULongLong();
        break;
    case QMetaType::UInt:
    case QMetaType::UShort:
    case QMetaType::UChar:
        u = v.toUInt();
        break;
    case QMetaType::Int:
    case QMetaType::Short:
    case QMetaType::LongLong:
        if (v.toLongLong() < 0)
            return 0;
        u = static_cast<qulonglong>(v.toLongLong());
        break;
    default:
        return 0;
    }
    return u > 0xFFFFFFFFULL ? 0 : static_cast<uint>(u);
}

QVariantMap hintsAt(const QVariantList &args, int i)
{
    QVariantMap out;
    if (i >= args.size())
        return out;
    const QVariant &v = args.at(i);
    QVariantMap m;
    if (v.type() == QVariant::Map)
        m = v.toMap();
    else if (v.type() == QVariant::Hash) {
        const QVariantHash h = v.toHash();
        for (QVariantHash::const_iterator it = h.constBegin(); it != h.constEnd(); ++it)
            m.insert(it.key(), it.value());
    } else
        return out;
    for (QVariantMap::const_iterator it = m.constBegin(); it != m.constEnd(); ++it) {
        QVariant val = it.value();
        if (val.userType() == qMetaTypeId<QDBusVariant>())
            val = qvariant_cast<QDBusVariant>(val).variant();
        out.insert(it.key(), val);
    }
    return out;
}

// libdbus message argument -> QVariant. Basic types keep their width; a{sv}
// and a{s?} become QVariantMap, arrays of strings QStringList, arrays of
// bytes QByteArray, other arrays and structs QVariantList. Anything else
// (file descriptors, too deep nesting) becomes an invalid QVariant.
QVariant fromIter(DBusMessageIter *it, int depth)
{
    if (depth > kMaxDepth)
        return QVariant();
    const int type = dbus_message_iter_get_arg_type(it);
    union {
        dbus_bool_t b;
        unsigned char y;
        dbus_int16_t n;
        dbus_uint16_t q;
        dbus_int32_t i;
        dbus_uint32_t u;
        dbus_int64_t x;
        dbus_uint64_t t;
        double d;
        const char *s;
    } v;
    switch (type) {
    case DBUS_TYPE_BOOLEAN:
        dbus_message_iter_get_basic(it, &v);
        return QVariant(v.b != 0);
    case DBUS_TYPE_BYTE:
        dbus_message_iter_get_basic(it, &v);
        return QVariant::fromValue<uchar>(v.y);
    case DBUS_TYPE_INT16:
        dbus_message_iter_get_basic(it, &v);
        return QVariant::fromValue<short>(v.n);
    case DBUS_TYPE_UINT16:
        dbus_message_iter_get_basic(it, &v);
        return QVariant::fromValue<ushort>(v.q);
    case DBUS_TYPE_INT32:
        dbus_message_iter_get_basic(it, &v);
        return QVariant(static_cast<int>(v.i));
    case DBUS_TYPE_UINT32:
        dbus_message_iter_get_basic(it, &v);
        return QVariant(static_cast<uint>(v.u));
    case DBUS_TYPE_INT64:
        dbus_message_iter_get_basic(it, &v);
        return QVariant(static_cast<qlonglong>(v.x));
    case DBUS_TYPE_UINT64:
        dbus_message_iter_get_basic(it, &v);
        return QVariant(static_cast<qulonglong>(v.t));
    case DBUS_TYPE_DOUBLE:
        dbus_message_iter_get_basic(it, &v);
        return QVariant(v.d);
    case DBUS_TYPE_STRING:
    case DBUS_TYPE_OBJECT_PATH:
    case DBUS_TYPE_SIGNATURE:
        dbus_message_iter_get_basic(it, &v);
        return QVariant(QString::fromUtf8(v.s ? v.s : ""));
    case DBUS_TYPE_VARIANT: {
        DBusMessageIter sub;
        dbus_message_iter_recurse(it, &sub);
        return fromIter(&sub, depth + 1);
    }
    case DBUS_TYPE_STRUCT: {
        QVariantList list;
        DBusMessageIter sub;
        dbus_message_iter_recurse(it, &sub);
        while (dbus_message_iter_get_arg_type(&sub) != DBUS_TYPE_INVALID) {
            list.append(fromIter(&sub, depth + 1));
            dbus_message_iter_next(&sub);
        }
        return QVariant(list);
    }
    case DBUS_TYPE_ARRAY: {
        const int et = dbus_message_iter_get_element_type(it);
        DBusMessageIter sub;
        dbus_message_iter_recurse(it, &sub);
        if (et == DBUS_TYPE_BYTE) {
            const unsigned char *p = 0;
            int n = 0;
            dbus_message_iter_get_fixed_array(&sub, &p, &n);
            return QVariant(n > 0 && p ? QByteArray(reinterpret_cast<const char *>(p), n)
                                       : QByteArray());
        }
        if (et == DBUS_TYPE_DICT_ENTRY) {
            QVariantMap map;
            while (dbus_message_iter_get_arg_type(&sub) == DBUS_TYPE_DICT_ENTRY) {
                DBusMessageIter e;
                dbus_message_iter_recurse(&sub, &e);
                const int kt = dbus_message_iter_get_arg_type(&e);
                if (kt == DBUS_TYPE_STRING || kt == DBUS_TYPE_OBJECT_PATH) {
                    const QString key = fromIter(&e, depth + 1).toString();
                    dbus_message_iter_next(&e);
                    map.insert(key, fromIter(&e, depth + 1));
                }
                dbus_message_iter_next(&sub);
            }
            return QVariant(map);
        }
        QVariantList list;
        while (dbus_message_iter_get_arg_type(&sub) != DBUS_TYPE_INVALID) {
            list.append(fromIter(&sub, depth + 1));
            dbus_message_iter_next(&sub);
        }
        if (et == DBUS_TYPE_STRING) {
            QStringList sl;
            for (int i = 0; i < list.size(); ++i)
                sl.append(list.at(i).toString());
            return QVariant(sl);
        }
        return QVariant(list);
    }
    default:
        return QVariant();
    }
}

QVariantList argsOf(DBusMessage *msg)
{
    QVariantList args;
    DBusMessageIter it;
    if (!dbus_message_iter_init(msg, &it))
        return args;
    while (dbus_message_iter_get_arg_type(&it) != DBUS_TYPE_INVALID && args.size() < kMaxArgs) {
        args.append(fromIter(&it, 0));
        dbus_message_iter_next(&it);
    }
    return args;
}

} // namespace

notiffilter::IncomingNotification parseNotify(const QVariantList &args, const QDateTime &when)
{
    notiffilter::IncomingNotification n;
    n.appName = stringAt(args, 0);
    n.replacesId = replacesIdAt(args, 1);
    // args[2] app_icon, [5] actions, [7] expire_timeout: not needed.
    n.summary = stringAt(args, 3);
    n.body = stringAt(args, 4);
    n.hints = hintsAt(args, 6);
    n.when = when;
    return n;
}

// Reaches the private members from the libdbus callbacks.
struct MonitorGlue {
    static DBusHandlerResult filter(DBusConnection *, DBusMessage *msg, void *data)
    {
        NotifMonitor *self = static_cast<NotifMonitor *>(data);
        if (dbus_message_is_signal(msg, DBUS_INTERFACE_LOCAL, "Disconnected")) {
            if (!self->m_lostQueued) {
                self->m_lostQueued = true;
                QMetaObject::invokeMethod(self, "handleLost", Qt::QueuedConnection);
            }
        } else if (dbus_message_is_method_call(msg, kNotifInterface, kNotifMember)) {
            self->onNotify(msg);
        }
        // Always "handled": libdbus must not answer with an error, a monitor sends nothing.
        return DBUS_HANDLER_RESULT_HANDLED;
    }
};

NotifMonitor::NotifMonitor(const QByteArray &busAddress, QObject *parent)
    : QObject(parent)
    , m_busAddress(busAddress)
    , m_conn(0)
    , m_notifier(0)
    , m_state(NotRunning)
    , m_reason(tr("Not started."))
    , m_wanted(false)
    , m_lostQueued(false)
    , m_debug(false)
    , m_failures(0)
{
    qRegisterMetaType<notiffilter::IncomingNotification>("notiffilter::IncomingNotification");
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, SIGNAL(timeout()), this, SLOT(retryNow()));
}

NotifMonitor::~NotifMonitor()
{
    m_wanted = false;
    teardown();
}

void NotifMonitor::start()
{
    m_wanted = true;
    if (m_conn)
        return;
    m_retryTimer.stop();
    m_failures = 0;
    connectNow();
}

void NotifMonitor::stop()
{
    m_wanted = false;
    m_retryTimer.stop();
    teardown();
    setState(NotRunning, tr("Stopped."));
}

void NotifMonitor::setState(State s, const QString &reason)
{
    if (s == m_state && reason == m_reason)
        return;
    m_state = s;
    m_reason = reason;
    emit stateChanged(static_cast<int>(s), reason);
}

void NotifMonitor::teardown()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = 0;
    }
    if (m_conn) {
        DBusConnection *c = m_conn;
        m_conn = 0;
        dbus_connection_close(c);
        dbus_connection_unref(c);
    }
    m_lostQueued = false;
    m_upTime.invalidate();
}

void NotifMonitor::scheduleRetry()
{
    if (!m_wanted)
        return;
    m_retryTimer.start(retryDelayMs(m_failures));
    if (m_failures < 30)
        ++m_failures;
}

void NotifMonitor::retryNow()
{
    if (m_wanted && !m_conn)
        connectNow();
}

void NotifMonitor::connectNow()
{
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *c = 0;
    if (m_busAddress.isEmpty()) {
        c = dbus_bus_get_private(DBUS_BUS_SESSION, &err);
    } else {
        c = dbus_connection_open_private(m_busAddress.constData(), &err);
        if (c && !dbus_bus_register(c, &err)) {
            dbus_connection_close(c);
            dbus_connection_unref(c);
            c = 0;
        }
    }
    if (!c) {
        if (m_debug)
            qDebug("notifmonitor: no bus connection (%s)", err.name ? err.name : "?");
        dbus_error_free(&err);
        setState(NotRunning, tr("Cannot connect to the session bus; retrying."));
        scheduleRetry();
        return;
    }
    dbus_connection_set_exit_on_disconnect(c, FALSE);

    int fd = -1;
    if (!dbus_connection_add_filter(c, &MonitorGlue::filter, this, 0)
            || !dbus_connection_get_unix_fd(c, &fd)) {
        dbus_connection_close(c);
        dbus_connection_unref(c);
        setState(NotRunning, tr("Cannot set up the session bus connection; retrying."));
        scheduleRetry();
        return;
    }

    // org.freedesktop.DBus.Monitoring.BecomeMonitor(as rules, u flags)
    static const char rule[] =
        "type='method_call',interface='org.freedesktop.Notifications',member='Notify'";
    DBusMessage *call = dbus_message_new_method_call("org.freedesktop.DBus", "/org/freedesktop/DBus",
                                                     "org.freedesktop.DBus.Monitoring",
                                                     "BecomeMonitor");
    if (!call) {
        dbus_connection_close(c);
        dbus_connection_unref(c);
        setState(NotRunning, tr("Out of memory; retrying."));
        scheduleRetry();
        return;
    }
    DBusMessageIter it, arr;
    const char *rulePtr = rule;
    dbus_uint32_t flags = 0;
    dbus_message_iter_init_append(call, &it);
    dbus_message_iter_open_container(&it, DBUS_TYPE_ARRAY, "s", &arr);
    dbus_message_iter_append_basic(&arr, DBUS_TYPE_STRING, &rulePtr);
    dbus_message_iter_close_container(&it, &arr);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &flags);

    DBusMessage *reply = dbus_connection_send_with_reply_and_block(c, call, kBecomeMonitorTimeoutMs, &err);
    dbus_message_unref(call);
    if (!reply) {
        const QByteArray name = err.name ? err.name : "";
        dbus_error_free(&err);
        dbus_connection_close(c);
        dbus_connection_unref(c);
        if (name == DBUS_ERROR_ACCESS_DENIED) {
            // Permanent: a sandboxed process is refused (observed under Sailjail).
            setState(CannotMonitor,
                     tr("The session bus refused monitoring (access denied, for example inside a sandbox)."));
        } else if (name == DBUS_ERROR_UNKNOWN_METHOD || name == DBUS_ERROR_NOT_SUPPORTED) {
            setState(CannotMonitor, tr("The session bus does not support monitoring."));
        } else {
            setState(NotRunning, tr("The monitoring request failed (%1); retrying.")
                                     .arg(QString::fromLatin1(name)));
            scheduleRetry();
        }
        if (m_debug)
            qDebug("notifmonitor: BecomeMonitor failed (%s)", name.constData());
        return;
    }
    dbus_message_unref(reply);

    m_conn = c;
    m_lostQueued = false;
    m_upTime.start();
    m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_notifier, SIGNAL(activated(int)), this, SLOT(onSocketActivity()));
    setState(Running, QString());
    if (m_debug)
        qDebug("notifmonitor: monitoring");
    // Messages may already be buffered from the reply read.
    pump();
}

void NotifMonitor::onSocketActivity()
{
    pump();
}

void NotifMonitor::pump()
{
    DBusConnection *c = m_conn;
    if (!c)
        return;
    dbus_connection_read_write(c, 0);
    // A handler may call stop(): m_conn is then 0 and c is not used again.
    while (m_conn == c && dbus_connection_dispatch(c) == DBUS_DISPATCH_DATA_REMAINS) {
    }
    if (m_conn == c && !dbus_connection_get_is_connected(c) && !m_lostQueued) {
        m_lostQueued = true;
        QMetaObject::invokeMethod(this, "handleLost", Qt::QueuedConnection);
    }
}

void NotifMonitor::handleLost()
{
    if (!m_conn)
        return;
    // A connection that stayed up for the longest delay starts the series again.
    if (m_upTime.isValid() && m_upTime.elapsed() >= kRetryMaxMs)
        m_failures = 0;
    teardown();
    if (m_debug)
        qDebug("notifmonitor: connection lost");
    setState(NotRunning, tr("Connection to the session bus lost; retrying."));
    scheduleRetry();
}

void NotifMonitor::onNotify(DBusMessage *msg)
{
    const QDateTime now = QDateTime::currentDateTime();
    const notiffilter::IncomingNotification n = parseNotify(argsOf(msg), now);
    if (m_debug)
        qDebug("notifmonitor: %s app_name=\"%s\" summary_len=%d body_len=%d replaces_id=%u hints=%d",
               qPrintable(now.toString(Qt::ISODate)), qPrintable(n.appName),
               n.summary.size(), n.body.size(), n.replacesId, n.hints.size());
    emit notificationReceived(n);
}

} // namespace notifmonitor
