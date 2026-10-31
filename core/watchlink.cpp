// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "watchlink.h"

namespace watchlink {

const char *const kUuidRequest = "26eb002c-b012-49a8-b1f8-394fb2032b0f";
const char *const kUuidData = "26eb002d-b012-49a8-b1f8-394fb2032b0f";
const char *const kUuidNotif = "26eb0030-b012-49a8-b1f8-394fb2032b0f";

int retryDelaySeconds(int failures)
{
    static const int steps[] = {30, 60, 120, 240};
    if (failures <= 0)
        return 0;
    return steps[qMin(failures, 4) - 1];
}

namespace {

QByteArray toByteArray(const watchproto::Bytes &b)
{
    return QByteArray(reinterpret_cast<const char *>(b.data()), static_cast<int>(b.size()));
}

std::string utf8(const QString &s)
{
    const QByteArray b = s.toUtf8();
    return std::string(b.constData(), static_cast<size_t>(b.size()));
}

QString secs(qint64 ms)
{
    return QString::number(ms / 1000.0, 'f', 2);
}

} // namespace

WatchLink::WatchLink(BluezAdapter *adapter, const watchproto::ModelTable *models,
                     const scheduler::ClockSource *clock)
    : m_adapter(adapter), m_models(models), m_clock(clock), m_log(0), m_nextId(1), m_busy(false), m_gapMs(kWriteGapMs)
{
}

void WatchLink::say(const QString &line)
{
    if (!m_log)
        return;
    const qint64 ms = m_since.isValid() ? m_since.elapsed() : 0;
    m_log->log(QStringLiteral("+%1 s  %2").arg(secs(ms), line));
}

// R-TIME-12: the phone clock, local wall-clock time (no zone in the frame), rounded to the nearest
// second.
watchproto::DateTime WatchLink::phoneTime(QDateTime *now) const
{
    QDateTime t = m_clock->now().toLocalTime();
    t = t.addMSecs(500);
    if (now)
        *now = t;
    return watchproto::DateTime(t.date().year(), t.date().month(), t.date().day(),
                                t.time().hour(), t.time().minute(), t.time().second());
}

QString WatchLink::bleAddressFromFeature10(const QByteArray &a)
{
    if (a.size() < 7 || static_cast<quint8>(a.at(0)) != kFeatureBleInfo)
        return QString();
    QStringList parts;
    for (int i = 6; i >= 1; --i)
        parts << QString::number(static_cast<quint8>(a.at(i)) + 0x100, 16).mid(1).toUpper();
    return parts.join(QLatin1Char(':'));
}

// ------------------------------------------------------------------ finding

QList<FoundWatch> WatchLink::findWatches(QString *error)
{
    QList<FoundWatch> found;
    QList<BluezDevice> devs;
    QString err;
    if (!m_adapter->devices(&devs, &err)) {
        if (error)
            *error = tr("Cannot read the Bluetooth devices: %1").arg(err);
        return found;
    }
    for (int i = 0; i < devs.size(); ++i) {
        if (!devs.at(i).paired)
            continue;
        const watchproto::ModelInfo *m = m_models->find(devs.at(i).name);
        if (!m)
            continue;
        FoundWatch fw;
        fw.device = devs.at(i);
        fw.model = m;
        found << fw;
    }
    return found;
}

// R-TIME-10: never a stored address as the only key. The entry's address is tried first; if BlueZ
// recreated the device, the single paired device with the entry's name is taken. With several
// watches of the same name the guess is refused, and the feature 0x10 check (expected BLE address)
// protects against taking the wrong one.
bool WatchLink::findDevice(const settings::WatchEntry &entry, QString *error, FoundWatch *out)
{
    QString err;
    const QList<FoundWatch> found = findWatches(&err);
    if (!err.isEmpty()) {
        *error = err;
        return false;
    }
    QList<int> sameName;
    for (int i = 0; i < found.size(); ++i) {
        const BluezDevice &d = found.at(i).device;
        if (d.address.compare(entry.address, Qt::CaseInsensitive) == 0
            || d.address.compare(entry.key, Qt::CaseInsensitive) == 0) {
            *out = found.at(i);
            return true;
        }
        if (!entry.deviceName.isEmpty() && d.name == entry.deviceName)
            sameName << i;
    }
    if (sameName.size() == 1) {
        *out = found.at(sameName.first());
        return true;
    }
    *error = sameName.isEmpty()
        ? tr("Watch not found among the paired devices")
        : tr("Several paired watches have this name; the watch cannot be told apart");
    return false;
}

// -------------------------------------------------------------------- queue

int WatchLink::enqueueAlert(const notiffilter::WatchNotification &n,
                            const QList<settings::WatchEntry> &targets)
{
    int dropped = 0;
    const QDateTime now = m_clock->now();
    for (int i = 0; i < targets.size(); ++i) {
        if (!targets.at(i).enabled)
            continue;
        QList<QueuedAlert> &q = m_queues[targets.at(i).key];
        QueuedAlert a;
        a.n = n;
        a.n.alert = true;                 // R-NOTIF-10: no silent mode
        a.enqueued = now;
        q.append(a);
        while (q.size() > kMaxQueued) {
            q.removeFirst();
            ++dropped;
        }
    }
    return dropped;
}

int WatchLink::queued(const QString &watchKey) const
{
    return m_queues.value(watchKey).size();
}

QDateTime WatchLink::nextAttempt(const QString &watchKey) const
{
    return m_nextAttempt.value(watchKey);
}

void WatchLink::clearQueue(const QString &watchKey)
{
    m_queues.remove(watchKey);
    m_failures.remove(watchKey);
    m_nextAttempt.remove(watchKey);
}

// ----------------------------------------------------------------- sessions

QList<SessionResult> WatchLink::runAll(const QList<SessionRequest> &requests)
{
    QList<SessionResult> results;
    for (int i = 0; i < requests.size(); ++i) {
        if (!requests.at(i).watch.enabled)
            continue;
        results << runSession(requests.at(i));
    }
    return results;
}

SessionResult WatchLink::runSession(const SessionRequest &request)
{
    if (m_busy) {
        SessionResult r;
        r.key = request.watch.key;
        r.when = m_clock->now();
        r.error = tr("Another session is running");
        return r;
    }
    // Alerts only, and the retry time has not come: leave the queue alone.
    if (!request.timeSync && m_nextAttempt.contains(request.watch.key)
        && m_clock->now() < m_nextAttempt.value(request.watch.key)
        && !m_queues.value(request.watch.key).isEmpty()) {
        SessionResult r;
        r.key = request.watch.key;
        r.when = m_clock->now();
        r.skipped = true;
        r.alertsKept = m_queues.value(request.watch.key).size();
        r.nextAttempt = m_nextAttempt.value(request.watch.key);
        return r;
    }
    m_busy = true;
    m_since.start();

    // Items older than 5 minutes are not sent late.
    QList<QueuedAlert> alerts = m_queues.take(request.watch.key);
    const qint64 now = m_clock->now().toMSecsSinceEpoch();
    int aged = 0;
    for (int i = alerts.size() - 1; i >= 0; --i) {
        if (now - alerts.at(i).enqueued.toMSecsSinceEpoch() > qint64(kMaxAgeSeconds) * 1000) {
            alerts.removeAt(i);
            ++aged;
        }
    }

    SessionResult r;
    if (!request.timeSync && alerts.isEmpty()) {
        r.key = request.watch.key;
        r.when = m_clock->now();
        r.skipped = true;
        say(QStringLiteral("nothing to do, no connection"));
    } else {
        r = session(request, alerts);
        const QString &key = request.watch.key;
        if (r.connected) {
            // The link worked: whatever was not delivered is lost.
            m_failures.remove(key);
            m_nextAttempt.remove(key);
        } else if (r.alertsKept > 0) {
            // No connection: back to the front of the queue (new alerts that
            // arrived meanwhile stay behind), circular, and try again later.
            QList<QueuedAlert> &q = m_queues[key];
            alerts += q;
            q = alerts;
            while (q.size() > kMaxQueued) {
                q.removeFirst();
                ++r.alertsDropped;
            }
            r.alertsKept = q.size();
            const int f = m_failures.value(key) + 1;
            m_failures[key] = f;
            r.nextAttempt = m_clock->now().addSecs(retryDelaySeconds(f));
            m_nextAttempt[key] = r.nextAttempt;
            say(QStringLiteral("%1 alert(s) kept, next attempt in %2 s")
                    .arg(q.size()).arg(retryDelaySeconds(f)));
        }
    }
    r.alertsDropped += aged;
    m_busy = false;
    return r;
}

bool WatchLink::buildNotifFrame(const watchproto::Notification &n, QByteArray *wire,
                                QString *error) const
{
    const watchproto::Notification fitted = watchproto::fitToWatch(n);
    const watchproto::NotifFrame f = watchproto::buildNotificationFrame(fitted);
    if (!f.ok()) {
        *error = tr("Invalid notification frame (code %1)").arg(int(f.error));
        return false;
    }
    *wire = toByteArray(f.bytes);
    return true;
}

SessionResult WatchLink::session(const SessionRequest &req, QList<QueuedAlert> alerts)
{
    SessionResult r;
    r.key = req.watch.key;
    r.when = m_clock->now();

    QString err;
    FoundWatch fw;
    if (!findDevice(req.watch, &err, &fw)) {
        say(QStringLiteral("device lookup failed"));
        r.error = err;
        r.alertsKept = alerts.size();
        return r;
    }
    const QString path = fw.device.path;
    r.deviceAddress = fw.device.address;
    const QString model = fw.model->id;
    say(QStringLiteral("watch found, model %1, %2 alert(s) queued, time sync %3")
            .arg(model).arg(alerts.size()).arg(req.timeSync ? "yes" : "no"));

    say(QStringLiteral("connect (timeout %1 s)").arg(kConnectTimeoutMs / 1000));
    if (!m_adapter->connectDevice(path, kConnectTimeoutMs, &err)) {
        say(QStringLiteral("connect failed"));
        r.error = tr("Cannot connect: %1").arg(err);
        r.alertsKept = alerts.size();        // out of range: kept for the next attempt
        return r;
    }
    r.connected = true;
    say(QStringLiteral("connected"));

    bool wrote = false;
    // Everything after the connection; returns an empty string on success.
    // Every exit goes through the disconnect below.
    QString failure;
    do {
        if (!m_adapter->waitServicesResolved(path, kServicesTimeoutMs, &err)) {
            failure = tr("Services not resolved: %1").arg(err);
            break;
        }
        say(QStringLiteral("services resolved"));

        if (!m_adapter->startNotify(path, QLatin1String(kUuidData), &err)) {
            failure = tr("Cannot enable notifications: %1").arg(err);
            break;
        }
        say(QStringLiteral("notify on 002d enabled"));

        // Identity (R-TIME-10): feature 0x10 holds the watch's own BLE address.
        if (req.readBleAddress || !req.expectedBleAddress.isEmpty()) {
            QByteArray answer;
            QString addr;
            if (m_adapter->readFeature(path, kFeatureBleInfo, kFeatureTimeoutMs, &answer, &err))
                addr = bleAddressFromFeature10(answer);
            r.bleAddress = addr;
            say(addr.isEmpty() ? QStringLiteral("feature 0x10 not read")
                               : QStringLiteral("feature 0x10 read"));
            if (!req.expectedBleAddress.isEmpty()) {
                if (addr.isEmpty()) {
                    failure = tr("The identity of the watch could not be verified");
                    break;
                }
                if (addr.compare(req.expectedBleAddress, Qt::CaseInsensitive) != 0) {
                    failure = tr("This is not the watch that was stored for this entry");
                    break;
                }
            }
        }

        if (req.timeSync) {
            // Phone time read at the moment of writing (R-TIME-6, R-TIME-12).
            const watchproto::TimeFrame tf =
                watchproto::buildTimeFrame(phoneTime(0), fw.model->dowEncoding);
            if (!tf.ok()) {
                failure = tr("The phone time is not valid for the watch (field %1)")
                              .arg(int(tf.invalidField));
                break;
            }
            if (!m_adapter->write(path, QLatin1String(kUuidData), toByteArray(tf.bytes), true,
                                  &err)) {
                failure = tr("Cannot write the time: %1").arg(err);
                break;
            }
            wrote = true;
            r.timeSent = true;
            say(QStringLiteral("time frame written (%1 bytes)").arg(tf.bytes.size()));
        }

        // Link status (R-NOTIF-11): after the time frame, before the alerts,
        // silent. A failed write fails the session.
        {
            QDateTime stamp;
            watchproto::Notification st;
            st.id = m_nextId++;
            st.alert = false;
            st.category = watchproto::kCategoryGeneric;
            st.timestamp = phoneTime(&stamp);
            st.sender = "CWS";
            st.title = utf8(tr("Sync"));
            QString msg = tr("Casio Watch Sync") + QLatin1Char('\n') + tr("Last contact with phone");
            if (!alerts.isEmpty())
                msg += QLatin1Char('\n') + QString::fromUtf8(alerts.last().n.sender.c_str());
            st.message = utf8(msg);
            QByteArray wire;
            if (wrote && m_gapMs > 0)
                m_adapter->settle(m_gapMs);
            if (!buildNotifFrame(st, &wire, &err)
                || !m_adapter->write(path, QLatin1String(kUuidNotif), wire, false, &err)) {
                failure = tr("Cannot write the link status: %1").arg(err);
                break;
            }
            wrote = true;
            say(QStringLiteral("link status written (id %1, %2 bytes)").arg(st.id).arg(wire.size()));
        }

        for (int i = 0; i < alerts.size(); ++i) {
            watchproto::Notification n;
            n.id = m_nextId++;
            n.alert = true;
            n.category = alerts.at(i).n.category;
            n.timestamp = phoneTime(0);
            n.sender = alerts.at(i).n.sender;
            n.title = alerts.at(i).n.title;
            n.subtitle = alerts.at(i).n.subtitle;
            n.message = alerts.at(i).n.message;
            QByteArray wire;
            QString buildErr;
            if (!buildNotifFrame(n, &wire, &buildErr)) {
                // A bug upstream, not a link problem: skip this one, go on.
                say(QStringLiteral("alert %1/%2 skipped: %3").arg(i + 1).arg(alerts.size(), 0, 10)
                        .arg(buildErr));
                continue;
            }
            if (m_gapMs > 0)
                m_adapter->settle(m_gapMs);
            if (!m_adapter->write(path, QLatin1String(kUuidNotif), wire, false, &err)) {
                failure = tr("Cannot write the notification: %1").arg(err);
                break;
            }
            wrote = true;
            ++r.alertsSent;
            say(QStringLiteral("alert %1/%2 written (id %3, %4 bytes)")
                    .arg(i + 1).arg(alerts.size()).arg(n.id).arg(wire.size()));
        }
    } while (false);

    if (wrote)
        m_adapter->settle(kSettleMs);
    m_adapter->disconnectDevice(path);
    say(QStringLiteral("disconnected"));

    r.error = failure;
    r.alertsDropped = alerts.size() - r.alertsSent;
    return r;
}

} // namespace watchlink
