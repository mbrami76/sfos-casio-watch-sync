// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace settings {

namespace {

const char kOrg[] = "it.michelebrami";

// Same rule as notiffilter::isBlacklisted; duplicated here so the module has no dependency on the
// other one. Entries are trimmed, empty ones are ignored.
bool matchesBlacklist(const QString &app, const QStringList &blacklist)
{
    for (int i = 0; i < blacklist.size(); ++i) {
        const QString e = blacklist.at(i).trimmed();
        if (!e.isEmpty() && app.contains(e, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

// Strict boolean: true/1 or false/0 (any case), anything else -> fallback.
bool readBool(const QSettings &s, const QString &key, bool fallback)
{
    const QString v = s.value(key).toString().trimmed().toLower();
    if (v == QLatin1String("true") || v == QLatin1String("1"))
        return true;
    if (v == QLatin1String("false") || v == QLatin1String("0"))
        return false;
    return fallback;
}

QStringList uniqueNonEmpty(const QStringList &in)
{
    QStringList out;
    for (int i = 0; i < in.size(); ++i)
        if (!in.at(i).isEmpty() && !out.contains(in.at(i)))
            out << in.at(i);
    return out;
}

} // namespace

QString defaultSettingsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
        + QLatin1Char('/') + QLatin1String(kOrg) + QLatin1String("/sfos-casio-sync.conf");
}

QString defaultStatePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QLatin1Char('/') + QLatin1String(kOrg) + QLatin1String("/sfos-casio-sync/state.json");
}

QString installedModelsPath()
{
    return QStringLiteral("/usr/share/sfos-casio-sync/models.json");
}

bool WatchEntry::operator==(const WatchEntry &o) const
{
    return key == o.key && address == o.address && deviceName == o.deviceName
        && modelId == o.modelId && alias == o.alias && silhouette == o.silhouette
        && enabled == o.enabled
        && riskAcknowledged == o.riskAcknowledged;
}

// ---------------------------------------------------------------- Settings

Settings::Settings(const QString &path, QObject *parent)
    : QObject(parent), m_path(path), m_s(path, QSettings::IniFormat)
{
    // QSettings creates the directory on the first write.
}

void Settings::commit()
{
    m_s.sync();
    emit changed();
}

void Settings::reload()
{
    const int interval = intervalMinutes();
    const QList<WatchEntry> w = watches();
    const bool master = masterOn(), dbg = debugLog();
    const QStringList bl = blacklist(), a = apps(), e = enabledApps();
    m_s.sync();
    if (interval != intervalMinutes() || w != watches() || master != masterOn()
        || dbg != debugLog() || bl != blacklist() || a != apps() || e != enabledApps())
        emit changed();
}

QStringList Settings::readList(const char *key) const
{
    return uniqueNonEmpty(m_s.value(QLatin1String(key)).toStringList());
}

void Settings::writeList(const char *key, const QStringList &v)
{
    if (v.isEmpty())
        m_s.remove(QLatin1String(key));
    else
        m_s.setValue(QLatin1String(key), v);
}

QList<int> allowedIntervalMinutes()
{
    QList<int> v;
    v << 360 << 720 << 1440 << 2880 << 10080 << 20160 << 43200;
    return v;
}

bool isAllowedInterval(int minutes)
{
    return allowedIntervalMinutes().contains(minutes);
}

int Settings::intervalMinutes() const
{
    bool ok = false;
    const int v = m_s.value(QStringLiteral("sync/intervalMinutes")).toInt(&ok);
    return (ok && isAllowedInterval(v)) ? v : kDefaultIntervalMinutes;
}

bool Settings::setIntervalMinutes(int minutes)
{
    if (!isAllowedInterval(minutes))
        return false;
    if (minutes == intervalMinutes())
        return true;
    m_s.setValue(QStringLiteral("sync/intervalMinutes"), minutes);
    commit();
    return true;
}

QList<WatchEntry> Settings::readWatches() const
{
    QList<WatchEntry> out;
    QSet<QString> keys;
    QSettings &s = m_s;
    const int n = s.beginReadArray(QStringLiteral("watches"));
    for (int i = 0; i < n; ++i) {
        s.setArrayIndex(i);
        WatchEntry e;
        e.key = s.value(QStringLiteral("key")).toString();
        if (e.key.isEmpty() || keys.contains(e.key))
            continue; // skip invalid entries instead of failing the whole list
        keys.insert(e.key);
        e.address = s.value(QStringLiteral("address")).toString();
        e.deviceName = s.value(QStringLiteral("deviceName")).toString();
        e.modelId = s.value(QStringLiteral("modelId")).toString();
        e.alias = s.value(QStringLiteral("alias")).toString();
        e.silhouette = s.value(QStringLiteral("silhouette")).toString();
        e.enabled = readBool(s, QStringLiteral("enabled"), false);
        e.riskAcknowledged = readBool(s, QStringLiteral("riskAcknowledged"), false);
        out << e;
    }
    s.endArray();
    return out;
}

void Settings::writeWatches(const QList<WatchEntry> &list)
{
    m_s.remove(QStringLiteral("watches"));
    if (list.isEmpty())
        return;
    m_s.beginWriteArray(QStringLiteral("watches"), list.size());
    for (int i = 0; i < list.size(); ++i) {
        m_s.setArrayIndex(i);
        const WatchEntry &e = list.at(i);
        m_s.setValue(QStringLiteral("key"), e.key);
        m_s.setValue(QStringLiteral("address"), e.address);
        m_s.setValue(QStringLiteral("deviceName"), e.deviceName);
        m_s.setValue(QStringLiteral("modelId"), e.modelId);
        m_s.setValue(QStringLiteral("alias"), e.alias);
        m_s.setValue(QStringLiteral("silhouette"), e.silhouette);
        m_s.setValue(QStringLiteral("enabled"), e.enabled);
        m_s.setValue(QStringLiteral("riskAcknowledged"), e.riskAcknowledged);
    }
    m_s.endArray();
}

QList<WatchEntry> Settings::watches() const
{
    return readWatches();
}

bool Settings::setWatches(const QList<WatchEntry> &list)
{
    QSet<QString> keys;
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).key.isEmpty() || keys.contains(list.at(i).key))
            return false;
        keys.insert(list.at(i).key);
    }
    if (list == readWatches())
        return true;
    writeWatches(list);
    commit();
    return true;
}

bool Settings::moveWatch(int from, int to)
{
    QList<WatchEntry> l = readWatches();
    if (from < 0 || from >= l.size() || to < 0 || to >= l.size())
        return false;
    if (from != to) {
        l.move(from, to);
        writeWatches(l);
        commit();
    }
    return true;
}

QStringList Settings::hiddenDevices() const
{
    return uniqueNonEmpty(m_s.value(QStringLiteral("watchlist/hidden")).toStringList());
}

bool Settings::setHiddenDevices(const QStringList &addresses)
{
    const QStringList clean = uniqueNonEmpty(addresses);
    if (clean == hiddenDevices())
        return true;
    writeList("watchlist/hidden", clean);
    commit();
    return true;
}

bool Settings::masterOn() const
{
    return readBool(m_s, QStringLiteral("notifications/masterOn"), false);
}

bool Settings::setMasterOn(bool on)
{
    if (on != masterOn()) {
        m_s.setValue(QStringLiteral("notifications/masterOn"), on);
        commit();
    }
    return true;
}

QStringList Settings::blacklist() const
{
    QStringList out;
    const QStringList raw = m_s.value(QStringLiteral("notifications/blacklist")).toStringList();
    for (int i = 0; i < raw.size(); ++i) {
        const QString e = raw.at(i).trimmed();
        if (!e.isEmpty())
            out << e;
    }
    return out;
}

bool Settings::setBlacklist(const QStringList &entries)
{
    QStringList clean;
    for (int i = 0; i < entries.size(); ++i) {
        const QString e = entries.at(i).trimmed();
        if (e.isEmpty())
            return false;
        clean << e;
    }
    if (clean == blacklist())
        return true;
    writeList("notifications/blacklist", clean);
    commit();
    return true;
}

bool Settings::debugLog() const
{
    return readBool(m_s, QStringLiteral("debug/log"), false);
}

bool Settings::setDebugLog(bool on)
{
    if (on != debugLog()) {
        m_s.setValue(QStringLiteral("debug/log"), on);
        commit();
    }
    return true;
}

QStringList Settings::apps() const
{
    const QStringList all = readList("notifications/apps");
    const QStringList bl = blacklist();
    QStringList out;
    for (int i = 0; i < all.size(); ++i)
        if (!matchesBlacklist(all.at(i), bl))
            out << all.at(i);
    return out;
}

QStringList Settings::enabledApps() const
{
    const QStringList listed = apps();
    const QStringList en = readList("notifications/enabledApps");
    QStringList out;
    for (int i = 0; i < listed.size(); ++i)
        if (en.contains(listed.at(i)))
            out << listed.at(i);
    return out;
}

bool Settings::isAppEnabled(const QString &name) const
{
    return enabledApps().contains(name);
}

bool Settings::discoverApp(const QString &name)
{
    if (name.trimmed().isEmpty() || matchesBlacklist(name, blacklist()))
        return false;
    QStringList listed = readList("notifications/apps");
    if (listed.contains(name))
        return false;
    listed << name;
    writeList("notifications/apps", listed);
    commit();
    return true;
}

bool Settings::removeApp(const QString &name)
{
    QStringList listed = readList("notifications/apps");
    if (!listed.contains(name))
        return false;
    listed.removeAll(name);
    writeList("notifications/apps", listed);
    QStringList en = readList("notifications/enabledApps");
    en.removeAll(name);
    writeList("notifications/enabledApps", en);
    commit();
    return true;
}

bool Settings::setAppEnabled(const QString &name, bool enabled)
{
    if (!apps().contains(name))
        return false;
    QStringList en = readList("notifications/enabledApps");
    if (enabled == en.contains(name))
        return true;
    if (enabled)
        en << name;
    else
        en.removeAll(name);
    writeList("notifications/enabledApps", en);
    commit();
    return true;
}

// ------------------------------------------------------------------- State

bool WatchState::operator==(const WatchState &o) const
{
    return lastSuccess == o.lastSuccess && lastError == o.lastError
        && lastAttempt == o.lastAttempt && consecutiveFailures == o.consecutiveFailures
        && lastOpTime == o.lastOpTime && lastOpKind == o.lastOpKind
        && bleAddress == o.bleAddress;
}

namespace {

QJsonValue timeToJson(const QDateTime &t)
{
    return t.isValid() ? QJsonValue(t.toUTC().toString(Qt::ISODate)) : QJsonValue();
}

QDateTime timeFromJson(const QJsonValue &v)
{
    if (!v.isString())
        return QDateTime();
    QDateTime t = QDateTime::fromString(v.toString(), Qt::ISODate);
    if (!t.isValid())
        return QDateTime();
    t.setTimeSpec(Qt::UTC);
    return t;
}

} // namespace

State::State(const QString &path) : m_path(path)
{
}

bool State::load()
{
    m_watches.clear();
    m_seenApps.clear();

    QFile f(m_path);
    if (!f.exists())
        return true;                    // first run: empty state
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return false;

    const QJsonObject root = doc.object();
    const QJsonObject w = root.value(QLatin1String("watches")).toObject();
    for (QJsonObject::const_iterator it = w.constBegin(); it != w.constEnd(); ++it) {
        if (it.key().isEmpty() || !it.value().isObject())
            continue;
        const QJsonObject o = it.value().toObject();
        WatchState ws;
        ws.lastSuccess = timeFromJson(o.value(QLatin1String("lastSuccess")));
        ws.lastError = o.value(QLatin1String("lastError")).toString();
        ws.lastAttempt = timeFromJson(o.value(QLatin1String("lastAttempt")));
        const int cf = o.value(QLatin1String("consecutiveFailures")).toInt(0);
        ws.consecutiveFailures = cf > 0 ? cf : 0;
        const QJsonObject op = o.value(QLatin1String("lastOp")).toObject();
        ws.lastOpTime = timeFromJson(op.value(QLatin1String("time")));
        ws.lastOpKind = op.value(QLatin1String("kind")).toString();
        ws.bleAddress = o.value(QLatin1String("bleAddress")).toString();
        m_watches.insert(it.key(), ws);
    }
    const QJsonArray apps = root.value(QLatin1String("seenApps")).toArray();
    for (int i = 0; i < apps.size(); ++i)
        if (apps.at(i).isString())
            addSeenApp(apps.at(i).toString());
    return true;
}

bool State::save()
{
    QJsonObject w;
    for (QMap<QString, WatchState>::const_iterator it = m_watches.constBegin();
         it != m_watches.constEnd(); ++it) {
        const WatchState &ws = it.value();
        QJsonObject o;
        o.insert(QLatin1String("lastSuccess"), timeToJson(ws.lastSuccess));
        o.insert(QLatin1String("lastError"), ws.lastError);
        o.insert(QLatin1String("lastAttempt"), timeToJson(ws.lastAttempt));
        o.insert(QLatin1String("consecutiveFailures"), ws.consecutiveFailures);
        QJsonObject op;
        op.insert(QLatin1String("time"), timeToJson(ws.lastOpTime));
        op.insert(QLatin1String("kind"), ws.lastOpKind);
        o.insert(QLatin1String("lastOp"), op);
        o.insert(QLatin1String("bleAddress"), ws.bleAddress);
        w.insert(it.key(), o);
    }
    QJsonObject root;
    root.insert(QLatin1String("version"), 1);
    root.insert(QLatin1String("watches"), w);
    root.insert(QLatin1String("seenApps"), QJsonArray::fromStringList(m_seenApps));

    if (!QDir().mkpath(QFileInfo(m_path).absolutePath()))
        return false;
    QSaveFile f(m_path);                // temporary file in the same directory, renamed on commit
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}

WatchState State::watch(const QString &key) const
{
    return m_watches.value(key);
}

bool State::hasWatch(const QString &key) const
{
    return m_watches.contains(key);
}

void State::setWatch(const QString &key, const WatchState &ws)
{
    if (!key.isEmpty())
        m_watches.insert(key, ws);
}

void State::removeWatch(const QString &key)
{
    m_watches.remove(key);
}

QStringList State::watchKeys() const
{
    return m_watches.keys();
}

QStringList State::seenApps() const
{
    return m_seenApps;
}

void State::addSeenApp(const QString &name)
{
    if (!name.isEmpty() && !m_seenApps.contains(name))
        m_seenApps << name;
}

void State::clearSeenApps()
{
    m_seenApps.clear();
}

// ------------------------------------------------------------------ models

bool loadModelTable(watchproto::ModelTable *table, const QString &installedPath,
                    const QString &fallbackPath, QString *error)
{
    QStringList errors;
    const QString paths[2] = {installedPath, fallbackPath};
    for (int i = 0; i < 2; ++i) {
        if (paths[i].isEmpty())
            continue;
        QFile f(paths[i]);
        if (!f.open(QIODevice::ReadOnly)) {
            errors << paths[i] + QLatin1String(": cannot be read");
            continue;
        }
        QString e;
        if (table->load(f.readAll(), &e))
            return true;
        errors << paths[i] + QLatin1String(": ") + e;
    }
    table->load(QByteArray("{\"version\":1,\"models\":[]}")); // leave it empty
    if (error)
        *error = errors.isEmpty() ? QLatin1String("no models.json path given")
                                  : errors.join(QLatin1String("; "));
    return false;
}

} // namespace settings
