// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// settings: all persistent data. `Settings` (QSettings INI), `State` (state.json, atomic writes)
// and loading of the shipped models.json. Requirements: R-GEN-3, R-GEN-5, R-TIME-9, R-NOTIF-1,
// R-NOTIF-11.
#ifndef SETTINGS_H
#define SETTINGS_H

#include "modeltable.h"

#include <QDateTime>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

namespace settings {

// One watch of the user's list; the order of the list is the order of the
// sessions (R-GEN-5). `key` is the address of the paired device (R-TIME-10).
struct WatchEntry {
    QString key;
    QString address;
    QString deviceName;
    QString modelId;
    QString alias;
    QString silhouette;     // silhouette id; empty = the model default (any string is stored)
    bool enabled;
    bool riskAcknowledged;
    WatchEntry() : enabled(false), riskAcknowledged(false) {}
    bool operator==(const WatchEntry &o) const;
    bool operator!=(const WatchEntry &o) const { return !(*this == o); }
};

// The sync intervals the user can choose, in minutes (R-TIME-9): 6 h, 12 h, 24 h, 2 d, 7 d, 2 w, 1
// month (30 days). Any other value is invalid.
QList<int> allowedIntervalMinutes();
bool isAllowedInterval(int minutes);
const int kDefaultIntervalMinutes = 24 * 60; // R-TIME-9: once a day

// Default locations (QStandardPaths): ~/.config/it.michelebrami/sfos-casio-sync.conf,
// ~/.local/share/it.michelebrami/sfos-casio-sync/state.json,
// /usr/share/sfos-casio-sync/models.json.
QString defaultSettingsPath();
QString defaultStatePath();
QString installedModelsPath();

class Settings : public QObject
{
    Q_OBJECT
public:
    // `path` is the INI file; injectable for tests. A missing or corrupt file
    // gives the defaults; invalid stored values fall back to their default.
    explicit Settings(const QString &path = defaultSettingsPath(), QObject *parent = 0);

    QString path() const { return m_path; }

    // Re-reads the file (the UI writes it, the service reloads). Emits
    // changed() if anything differs from what was held before.
    void reload();

    // Setters validate, write the file at once and emit changed() only when the
    // value really changes. They return false (and change nothing) on invalid input.
    // Only values of allowedIntervalMinutes() are accepted; a stored value that
    // is not in the list reads as the default.
    int intervalMinutes() const;
    bool setIntervalMinutes(int minutes);

    QList<WatchEntry> watches() const;
    // Keys must be non-empty and unique; the list order is kept.
    bool setWatches(const QList<WatchEntry> &list);
    bool moveWatch(int from, int to);

    // Addresses of paired devices the user forgot. A paired device that is not in the watch list
    // is offered again at every listing, so forgetting it needs a memory of its own;
    // without it a listed-only row could never be removed. Adding the watch again clears it.
    // Entries are stored trimmed, without blanks or duplicates.
    QStringList hiddenDevices() const;
    bool setHiddenDevices(const QStringList &addresses);

    bool masterOn() const;                 // default false
    bool setMasterOn(bool on);

    QStringList blacklist() const;         // default empty
    // No empty (or blank) entries; entries are stored trimmed.
    bool setBlacklist(const QStringList &entries);

    bool debugLog() const;                 // default false
    bool setDebugLog(bool on);

    // ---- app list (R-NOTIF-1) Listed apps in the order discovered, without the blacklisted ones
    // (a blacklisted app is never shown, even if it was listed before).
    QStringList apps() const;
    QStringList enabledApps() const;       // subset of apps()
    bool isAppEnabled(const QString &name) const;
    // Called for a seen notification. Inserts the app disabled and returns true, unless it is
    // blacklisted, has an empty name or is already listed. An app removed earlier is re-inserted (a
    // call means a new notification); nothing is kept about removed apps.
    bool discoverApp(const QString &name);
    // Removes from the list (and from the enabled apps); false if not listed.
    bool removeApp(const QString &name);
    // false if the app is not in apps().
    bool setAppEnabled(const QString &name, bool enabled);

signals:
    void changed();

private:
    QStringList readList(const char *key) const;
    void writeList(const char *key, const QStringList &v);
    QList<WatchEntry> readWatches() const;
    void writeWatches(const QList<WatchEntry> &list);
    void commit();   // sync to disk, emit changed()

    QString m_path;
    mutable QSettings m_s;
};

// ------------------------------------------------------------------- State

// Per-watch state, keyed by WatchEntry::key. Times are stored in UTC.
struct WatchState {
    QDateTime lastSuccess;      // null if never
    QString lastError;          // short text or code, empty if none
    QDateTime lastAttempt;      // null if never (scheduler back-off)
    int consecutiveFailures;    // scheduler back-off
    QDateTime lastOpTime;       // last remote operation (R-NOTIF-11)
    QString lastOpKind;         // "timesync" or "notif", free text for the reader
    QString bleAddress;         // from feature 0x10 (R-TIME-10), empty if unknown
    WatchState() : consecutiveFailures(0) {}
    bool operator==(const WatchState &o) const;
};

class State
{
public:
    explicit State(const QString &path = defaultStatePath());

    QString path() const { return m_path; }

    // Missing or corrupt file -> empty state, returns false only for a
    // corrupt (unparsable) file; never crashes. Bad single fields are ignored.
    bool load();
    // Atomic (QSaveFile: temporary file in the same directory, then rename).
    bool save();

    WatchState watch(const QString &key) const;       // empty state if unknown
    bool hasWatch(const QString &key) const;
    void setWatch(const QString &key, const WatchState &ws);
    void removeWatch(const QString &key);
    QStringList watchKeys() const;

    QStringList seenApps() const;
    void addSeenApp(const QString &name);              // no duplicates, empty ignored
    void clearSeenApps();

private:
    QString m_path;
    QMap<QString, WatchState> m_watches;
    QStringList m_seenApps;
};

// ------------------------------------------------------------------ models

// Loads the model table from the first readable and valid file among
// `installedPath` and `fallbackPath` (empty paths skipped). False with *error
// if none works; the table is then empty.
bool loadModelTable(watchproto::ModelTable *table, const QString &installedPath,
                    const QString &fallbackPath = QString(), QString *error = 0);

} // namespace settings

#endif // SETTINGS_H
