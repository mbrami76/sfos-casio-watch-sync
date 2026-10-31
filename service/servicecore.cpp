// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "servicecore.h"

#include "notifmonitor.h"
#include "servicelog.h"

#include <QLocale>
#include <QTimer>

namespace service {

namespace {

qlonglong secs(const QDateTime &t)
{
    return t.isValid() ? t.toMSecsSinceEpoch() / 1000 : 0;
}

QString shortError(const QString &s)
{
    return s.size() > 200 ? s.left(200) : s;
}

} // namespace

ServiceCore::ServiceCore(const ServiceDeps &d, QObject *parent)
    : QObject(parent), m_settings(d.settings), m_state(d.state), m_models(d.models),
      m_clock(d.clock), m_wake(d.wake), m_runner(d.runner), m_monitor(d.monitor),
      m_publisher(d.publisher), m_started(false), m_selfChange(false),
      m_running(false), m_listing(0), m_nextTicket(1), m_clearQueues(false),
      m_masterWas(false), m_residentPublished(false)
{
    qRegisterMetaType<notiffilter::IncomingNotification>();
    connect(m_wake, SIGNAL(wake()), this, SLOT(onWake()));
    connect(m_settings, SIGNAL(changed()), this, SLOT(onSettingsChanged()));
    connect(m_monitor, SIGNAL(notificationReceived(notiffilter::IncomingNotification)), this,
            SLOT(onNotification(notiffilter::IncomingNotification)));
    connect(m_monitor, SIGNAL(stateChanged(int, QString)), this, SLOT(onMonitorState()));
    connect(m_runner, SIGNAL(jobFinished(QList<watchlink::SessionResult>)), this,
            SLOT(onJobFinished(QList<watchlink::SessionResult>)));
    connect(m_runner, SIGNAL(watchesListed(QList<service::FoundDevice>, QString)), this,
            SLOT(onWatchesListed(QList<service::FoundDevice>, QString)));
}

// ------------------------------------------------------------------ helpers

const watchproto::ModelInfo *ServiceCore::modelFor(const settings::WatchEntry &w) const
{
    const QList<watchproto::ModelInfo> &all = m_models->models();
    for (int i = 0; i < all.size(); ++i)
        if (all.at(i).id == w.modelId)
            return &all.at(i);
    return m_models->find(w.deviceName);
}

QString ServiceCore::defaultSilhouetteOf(const QString &modelId) const
{
    const QList<watchproto::ModelInfo> &all = m_models->models();
    for (int i = 0; i < all.size(); ++i)
        if (all.at(i).id == modelId)
            return all.at(i).defaultSilhouette;
    return QString();
}

bool ServiceCore::usable(const settings::WatchEntry &w, QString *why) const
{
    if (!w.enabled) {
        if (why)
            *why = tr("disabled");
        return false;
    }
    const watchproto::ModelInfo *m = modelFor(w);
    if (!m) {
        if (why)
            *why = tr("unknown model");
        return false;
    }
    if (m->status != watchproto::ModelSupported && !w.riskAcknowledged) {
        if (why)
            *why = tr("untested model, not acknowledged");
        return false;
    }
    return true;
}

QList<scheduler::WatchRunState> ServiceCore::runStates() const
{
    QList<scheduler::WatchRunState> out;
    const QList<settings::WatchEntry> list = m_settings->watches();
    for (int i = 0; i < list.size(); ++i) {
        if (!usable(list.at(i)))
            continue;
        const settings::WatchState ws = m_state->watch(list.at(i).key);
        scheduler::WatchRunState rs;
        rs.key = list.at(i).key;
        rs.enabled = true;
        rs.lastSuccess = ws.lastSuccess;
        rs.lastAttempt = ws.lastAttempt;
        rs.consecutiveFailures = ws.consecutiveFailures;
        out.append(rs);
    }
    return out;
}

notiffilter::NotifSettings ServiceCore::notifSettings() const
{
    notiffilter::NotifSettings s;
    s.masterOn = m_settings->masterOn();
    const QStringList apps = m_settings->enabledApps();
    for (int i = 0; i < apps.size(); ++i)
        s.enabledApps.insert(apps.at(i));
    s.blacklist = m_settings->blacklist();
    s.categoryByApp = notiffilter::defaultCategoryTable();
    s.ownAppName = notifmonitor::ownAppName();
    return s;
}

QString ServiceCore::formatTime(const QDateTime &t) const
{
    const QDateTime local = t.toLocalTime();
    const QDate today = m_clock->now().toLocalTime().date();
    return QLocale::c().toString(local, local.date() == today ? QStringLiteral("HH:mm")
                                                              : QStringLiteral("d MMM HH:mm"));
}

void ServiceCore::startRun()
{
    m_running = true;
    m_wake->hold();
}

void ServiceCore::endRun()
{
    m_running = false;
    m_wake->release();
}

// ---------------------------------------------------------------- life cycle

void ServiceCore::start()
{
    if (m_started)
        return;
    m_started = true;
    if (!m_state->load())
        debugLog(QStringLiteral("state file unreadable, starting with an empty state"));
    setDebugLog(m_settings->debugLog());
    m_monitor->setDebugLogging(m_settings->debugLog());
    m_masterWas = m_settings->masterOn();
    applyMaster();
    updateResident();
    evaluate();
}

void ServiceCore::applyMaster()
{
    const bool on = m_settings->masterOn();
    if (on) {
        m_monitor->start();             // safe when already running
    } else {
        m_monitor->stop();
        m_pending.clear();
        m_retryAt.clear();
        if (m_masterWas)
            m_clearQueues = true;       // alerts kept in the worker go too
    }
    m_masterWas = on;
}

void ServiceCore::onWake()
{
    if (!m_started)
        return;
    debugLog(QStringLiteral("wake"));
    applyMaster();      // a monitor that lost its connection is restarted here
    evaluate();
}

void ServiceCore::reloadSettings()
{
    m_selfChange = true;
    m_settings->reload();
    m_selfChange = false;
    onSettingsChanged();       // also when nothing differs: re-evaluate, cheap
}

void ServiceCore::onSettingsChanged()
{
    if (m_selfChange || !m_started)
        return;
    setDebugLog(m_settings->debugLog());
    m_monitor->setDebugLogging(m_settings->debugLog());
    applyMaster();
    updateResident();
    emit statusChanged();
    evaluate();
}

void ServiceCore::onMonitorState()
{
    debugLog(QStringLiteral("monitor state %1").arg(m_monitor->state()));
    emit statusChanged();
}

// ------------------------------------------------------------- scheduling

void ServiceCore::evaluate()
{
    const QDateTime now = m_clock->now();
    const scheduler::Plan p =
        scheduler::decide(now, qint64(m_settings->intervalMinutes()) * 60, runStates());
    for (int i = 0; i < p.due.size(); ++i)
        if (!m_inflight.contains(p.due.at(i).key))
            m_syncKeys.insert(p.due.at(i).key);
    pump();
    if (!m_running)
        rescheduleWake();
}

void ServiceCore::rescheduleWake()
{
    const QDateTime now = m_clock->now();
    const scheduler::Plan p =
        scheduler::decide(now, qint64(m_settings->intervalMinutes()) * 60, runStates());
    QDateTime next = p.nextWake;
    // Alerts waiting for a retry: wake at WatchLink::nextAttempt().
    for (QMap<QString, QDateTime>::const_iterator it = m_retryAt.constBegin();
         it != m_retryAt.constEnd(); ++it)
        if (it.value().isValid() && (!next.isValid() || it.value() < next))
            next = it.value();
    if (!next.isValid() && m_settings->masterOn())
        next = now.addSecs(scheduler::kDefaultMaxSleepSeconds);   // keeps the monitor honest
    if (next.isValid()) {
        m_wake->schedule(next);     // replaces a pending request
        debugLog(QStringLiteral("next wake in about %1 s").arg(now.secsTo(next)));
    } else {
        m_wake->cancel();
    }
}

// ------------------------------------------------------------------- jobs

void ServiceCore::pump()
{
    if (m_running)
        return;
    if (!m_listTickets.isEmpty()) {
        m_listing = m_listTickets.takeFirst();
        startRun();
        m_runner->listWatches();
        return;
    }

    const QDateTime now = m_clock->now();
    for (int i = m_pending.size() - 1; i >= 0; --i)
        if (m_pending.at(i).received.secsTo(now) > watchlink::kMaxAgeSeconds)
            m_pending.removeAt(i);
    while (m_pending.size() > watchlink::kMaxQueued)
        m_pending.removeFirst();

    SessionJob job;
    const QList<settings::WatchEntry> all = m_settings->watches();
    QList<settings::WatchEntry> targets;
    for (int i = 0; i < all.size(); ++i)
        if (usable(all.at(i)))
            targets.append(all.at(i));

    if (m_clearQueues) {
        for (int i = 0; i < all.size(); ++i)
            job.clearKeys << all.at(i).key;
        m_clearQueues = false;
    }
    if (!targets.isEmpty()) {
        for (int i = 0; i < m_pending.size(); ++i)
            job.alerts << m_pending.at(i).n;
        job.alertTargets = targets;
    }
    m_pending.clear();

    for (int i = 0; i < targets.size(); ++i) {
        const settings::WatchEntry &w = targets.at(i);
        const bool sync = m_syncKeys.contains(w.key);
        const QDateTime retry = m_retryAt.value(w.key);
        const bool alerts = !job.alerts.isEmpty() || (retry.isValid() && retry <= now);
        if (!sync && !alerts)
            continue;
        watchlink::SessionRequest r;
        r.watch = w;
        r.timeSync = sync;
        r.expectedBleAddress = m_state->watch(w.key).bleAddress;
        r.readBleAddress = r.expectedBleAddress.isEmpty();
        job.requests << r;
        m_inflight[w.key] = sync;
    }
    m_syncKeys.clear();

    if (job.alerts.isEmpty() && job.requests.isEmpty() && job.clearKeys.isEmpty())
        return;
    debugLog(QStringLiteral("job: %1 alert(s), %2 session(s)")
                 .arg(job.alerts.size()).arg(job.requests.size()));
    startRun();
    m_runner->submit(job);
    emit statusChanged();       // running flags
}

void ServiceCore::onJobFinished(const QList<watchlink::SessionResult> &results)
{
    const QDateTime now = m_clock->now();
    QSet<QString> handled;
    for (int i = 0; i < results.size(); ++i) {
        const watchlink::SessionResult &r = results.at(i);
        if (!m_inflight.contains(r.key))
            continue;
        const bool sync = m_inflight.value(r.key);
        handled.insert(r.key);
        debugLog(QStringLiteral("result: sync=%1 skipped=%2 connected=%3 timeSent=%4 "
                                "alertsSent=%5 dropped=%6 kept=%7 error=%8")
                     .arg(sync).arg(r.skipped).arg(r.connected).arg(r.timeSent)
                     .arg(r.alertsSent).arg(r.alertsDropped).arg(r.alertsKept)
                     .arg(r.error.isEmpty() ? QStringLiteral("-") : shortError(r.error)));
        if (r.skipped && !sync) {
            // Skipped because the retry time of kept alerts has not come yet
            // (or because there was nothing to send): keep the retry wake-up.
            if (r.alertsKept > 0 && r.nextAttempt.isValid())
                m_retryAt[r.key] = r.nextAttempt;
            else
                m_retryAt.remove(r.key);
            continue;
        }
        settings::WatchState ws = m_state->watch(r.key);
        const QDateTime when = r.when.isValid() ? r.when : now;
        if (sync) {
            ws.lastAttempt = when;
            if (r.ok() && r.timeSent) {
                ws.lastSuccess = when;
                ws.consecutiveFailures = 0;
            } else {
                ++ws.consecutiveFailures;
            }
        }
        if (r.ok()) {
            if (r.connected && (r.timeSent || r.alertsSent > 0)) {
                ws.lastOpTime = when;
                ws.lastOpKind = r.alertsSent > 0 ? QStringLiteral("notif")
                                                 : QStringLiteral("timesync");
            }
            if (!sync || r.timeSent)
                ws.lastError.clear();
            else
                ws.lastError = tr("time not sent");
        } else {
            ws.lastError = shortError(r.error);
        }
        if (!r.bleAddress.isEmpty())
            ws.bleAddress = r.bleAddress;
        m_state->setWatch(r.key, ws);
        if (r.alertsKept > 0 && r.nextAttempt.isValid())
            m_retryAt[r.key] = r.nextAttempt;
        else
            m_retryAt.remove(r.key);
    }
    // A time sync that came back without a result counts as a failed attempt,
    // otherwise the same watch would be asked again at once.
    for (QMap<QString, bool>::const_iterator it = m_inflight.constBegin();
         it != m_inflight.constEnd(); ++it) {
        if (!it.value() || handled.contains(it.key()))
            continue;
        settings::WatchState ws = m_state->watch(it.key());
        ws.lastAttempt = now;
        ++ws.consecutiveFailures;
        ws.lastError = tr("session did not run");
        m_state->setWatch(it.key(), ws);
    }
    m_inflight.clear();
    // The state file keeps one record per watch key. When a watch is forgotten, or comes back
    // under a new Bluetooth address, its old record would stay for ever and show up as a ghost
    // watch with stale failures, so records of keys that are no longer in the settings are dropped.
    {
        const QList<settings::WatchEntry> kept = m_settings->watches();
        QStringList keys;
        for (int i = 0; i < kept.size(); ++i)
            keys << kept.at(i).key;
        const QStringList stored = m_state->watchKeys();
        for (int i = 0; i < stored.size(); ++i)
            if (!keys.contains(stored.at(i)))
                m_state->removeWatch(stored.at(i));
    }
    if (!m_state->save())
        debugLog(QStringLiteral("state file could not be written"));
    endRun();
    updateResident();
    emit statusChanged();
    evaluate();
}

// ---------------------------------------------------------- notifications

void ServiceCore::onNotification(const notiffilter::IncomingNotification &in)
{
    const notiffilter::FilterResult fr = notiffilter::filter(in, notifSettings());
    debugLog(QStringLiteral("notification app=\"%1\" decision=%2")
                 .arg(in.appName).arg(int(fr.decision)));
    if (fr.discoverApp) {
        m_selfChange = true;
        const bool added = m_settings->discoverApp(in.appName);
        m_selfChange = false;
        if (added)
            emit statusChanged();
    }
    if (fr.forward())
        queueAlert(fr.n);
}

void ServiceCore::queueAlert(const notiffilter::WatchNotification &n)
{
    PendingAlert p;
    p.n = n;
    p.received = m_clock->now();
    m_pending.append(p);
    pump();
}

bool ServiceCore::testNotification()
{
    bool any = false;
    const QList<settings::WatchEntry> list = m_settings->watches();
    for (int i = 0; i < list.size(); ++i)
        any = any || usable(list.at(i));
    if (!any)
        return false;
    notiffilter::WatchNotification n;
    n.alert = true;
    n.category = 0;
    n.sender = notiffilter::shortenSender(notifmonitor::ownAppName());
    n.title = tr("Test").toUtf8().constData();
    n.message = tr("Test notification from Casio Watch Sync").toUtf8().constData();
    queueAlert(n);
    return true;
}

// -------------------------------------------------------------------- sync

bool ServiceCore::syncNow(const QString &key)
{
    bool any = false;
    const QList<settings::WatchEntry> list = m_settings->watches();
    for (int i = 0; i < list.size(); ++i) {
        if (!key.isEmpty() && list.at(i).key != key)
            continue;
        if (!usable(list.at(i)))
            continue;
        m_syncKeys.insert(list.at(i).key);
        any = true;
    }
    if (any)
        pump();
    return any;
}

// ------------------------------------------------------------- watch list

quint32 ServiceCore::requestWatchList()
{
    const quint32 ticket = m_nextTicket++;
    m_listTickets.append(ticket);
    // Not from here: the caller needs the ticket before an answer can come.
    QTimer::singleShot(0, this, SLOT(onPumpNow()));
    return ticket;
}

void ServiceCore::onPumpNow()
{
    pump();
}

void ServiceCore::onWatchesListed(const QList<service::FoundDevice> &devices, const QString &error)
{
    const quint32 ticket = m_listing;
    m_listing = 0;
    endRun();

    const QList<settings::WatchEntry> entries = m_settings->watches();
    QList<QVariantMap> out;
    QSet<QString> seen;
    for (int i = 0; i < devices.size(); ++i) {
        const FoundDevice &d = devices.at(i);
        QVariantMap m;
        m.insert(QStringLiteral("key"), d.address);
        m.insert(QStringLiteral("deviceName"), d.name);
        m.insert(QStringLiteral("bluezAlias"), d.alias);
        m.insert(QStringLiteral("modelId"), d.modelId);
        m.insert(QStringLiteral("modelLabel"), d.modelLabel);
        m.insert(QStringLiteral("supportStatus"),
                 d.supported ? QStringLiteral("supported") : QStringLiteral("untested"));
        m.insert(QStringLiteral("silhouette"), QString());
        m.insert(QStringLiteral("defaultSilhouette"), defaultSilhouetteOf(d.modelId));
        m.insert(QStringLiteral("foundNow"), true);
        m.insert(QStringLiteral("connected"), d.connected);
        bool inSettings = false;
        for (int j = 0; j < entries.size(); ++j)
            if (entries.at(j).key == d.address || entries.at(j).address == d.address) {
                inSettings = true;
                m.insert(QStringLiteral("key"), entries.at(j).key);
                m.insert(QStringLiteral("silhouette"), entries.at(j).silhouette);
                seen.insert(entries.at(j).key);
            }
        m.insert(QStringLiteral("inSettings"), inSettings);
        out.append(m);
    }
    for (int j = 0; j < entries.size(); ++j) {
        if (seen.contains(entries.at(j).key))
            continue;
        const watchproto::ModelInfo *mi = modelFor(entries.at(j));
        QVariantMap m;
        m.insert(QStringLiteral("key"), entries.at(j).key);
        m.insert(QStringLiteral("deviceName"), entries.at(j).deviceName);
        m.insert(QStringLiteral("bluezAlias"), QString());
        m.insert(QStringLiteral("modelId"), entries.at(j).modelId);
        m.insert(QStringLiteral("modelLabel"), mi ? mi->label : QString());
        m.insert(QStringLiteral("supportStatus"),
                 !mi ? QStringLiteral("unknown")
                     : (mi->status == watchproto::ModelSupported ? QStringLiteral("supported")
                                                                 : QStringLiteral("untested")));
        m.insert(QStringLiteral("silhouette"), entries.at(j).silhouette);
        m.insert(QStringLiteral("defaultSilhouette"), mi ? mi->defaultSilhouette : QString());
        m.insert(QStringLiteral("foundNow"), false);
        m.insert(QStringLiteral("connected"), false);
        m.insert(QStringLiteral("inSettings"), true);
        out.append(m);
    }
    emit watchListReady(ticket, out, error);
    evaluate();     // also starts the next waiting list request or job
}

// ------------------------------------------------------------------ status

QVariantMap ServiceCore::watchStatus(const settings::WatchEntry &w) const
{
    const watchproto::ModelInfo *mi = modelFor(w);
    const settings::WatchState ws = m_state->watch(w.key);
    QString why;
    const bool ok = usable(w, &why);
    QVariantMap m;
    m.insert(QStringLiteral("key"), w.key);
    m.insert(QStringLiteral("alias"), w.alias);
    m.insert(QStringLiteral("deviceName"), w.deviceName);
    m.insert(QStringLiteral("modelId"), w.modelId);
    m.insert(QStringLiteral("modelLabel"), mi ? mi->label : QString());
    m.insert(QStringLiteral("supportStatus"),
             !mi ? QStringLiteral("unknown")
                 : (mi->status == watchproto::ModelSupported ? QStringLiteral("supported")
                                                             : QStringLiteral("untested")));
    m.insert(QStringLiteral("silhouette"), w.silhouette);
    m.insert(QStringLiteral("defaultSilhouette"), mi ? mi->defaultSilhouette : QString());
    m.insert(QStringLiteral("enabled"), w.enabled);
    m.insert(QStringLiteral("riskAcknowledged"), w.riskAcknowledged);
    m.insert(QStringLiteral("usable"), ok);
    m.insert(QStringLiteral("problem"), (ok || !w.enabled) ? QString() : why);
    m.insert(QStringLiteral("running"), m_inflight.value(w.key, false));
    m.insert(QStringLiteral("lastSuccess"), secs(ws.lastSuccess));
    m.insert(QStringLiteral("lastAttempt"), secs(ws.lastAttempt));
    m.insert(QStringLiteral("lastError"), ws.lastError);
    m.insert(QStringLiteral("lastOpTime"), secs(ws.lastOpTime));
    m.insert(QStringLiteral("lastOpKind"), ws.lastOpKind);
    m.insert(QStringLiteral("consecutiveFailures"), ws.consecutiveFailures);
    qlonglong next = 0;
    if (ok) {
        scheduler::WatchRunState rs;
        rs.key = w.key;
        rs.lastSuccess = ws.lastSuccess;
        rs.lastAttempt = ws.lastAttempt;
        rs.consecutiveFailures = ws.consecutiveFailures;
        const QDateTime now = m_clock->now();
        const scheduler::Plan p = scheduler::decide(
            now, qint64(m_settings->intervalMinutes()) * 60, QList<scheduler::WatchRunState>() << rs);
        next = secs(p.due.isEmpty() ? p.nextWake : now);
    }
    m.insert(QStringLiteral("nextSync"), next);
    return m;
}

QVariantMap ServiceCore::status() const
{
    QVariantMap m;
    m.insert(QStringLiteral("serviceState"), QStringLiteral("running"));
    m.insert(QStringLiteral("busy"), m_running);
    m.insert(QStringLiteral("masterOn"), m_settings->masterOn());
    m.insert(QStringLiteral("intervalMinutes"), m_settings->intervalMinutes());
    m.insert(QStringLiteral("modelTableLoaded"), m_models->count() > 0);

    QString mstate;
    if (!m_settings->masterOn())
        mstate = QStringLiteral("off");
    else if (m_monitor->state() == notifmonitor::NotifMonitor::Running)
        mstate = QStringLiteral("running");
    else if (m_monitor->state() == notifmonitor::NotifMonitor::CannotMonitor)
        mstate = QStringLiteral("cannot-monitor");
    else
        mstate = QStringLiteral("not-running");
    m.insert(QStringLiteral("monitorState"), mstate);
    m.insert(QStringLiteral("monitorReason"), m_monitor->reason());

    QStringList risk;
    QList<QVariantMap> watches;
    const QList<settings::WatchEntry> list = m_settings->watches();
    for (int i = 0; i < list.size(); ++i) {
        const settings::WatchEntry &w = list.at(i);
        const watchproto::ModelInfo *mi = modelFor(w);
        if (w.enabled && mi && mi->status != watchproto::ModelSupported && !w.riskAcknowledged)
            risk << w.key;
        watches.append(watchStatus(w));
    }
    m.insert(QStringLiteral("riskProblems"), risk);
    m.insert(QStringLiteral("watches"), QVariant::fromValue(watches));
    return m;
}

// ---------------------------------------------------- resident notification

QString ServiceCore::residentBody() const
{
    bool any = false, never = false;
    QDateTime oldest;
    const QList<settings::WatchEntry> list = m_settings->watches();
    for (int i = 0; i < list.size(); ++i) {
        if (!usable(list.at(i)))
            continue;
        any = true;
        const QDateTime t = m_state->watch(list.at(i).key).lastSuccess;
        if (!t.isValid())
            never = true;
        else if (!oldest.isValid() || t < oldest)
            oldest = t;
    }
    if (!any)
        return tr("No watch enabled");
    if (never)
        return tr("Not synced yet");
    return tr("Last sync %1").arg(formatTime(oldest));
}

void ServiceCore::updateResident()
{
    const QString body = residentBody();
    if (m_residentPublished && body == m_lastResident)
        return;
    m_publisher->publish(notifmonitor::ownAppName(), body);
    m_lastResident = body;
    m_residentPublished = true;
}

} // namespace service
