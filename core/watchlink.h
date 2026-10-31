// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// watchlink: the only component that owns the BLE session to the watches. Finds the paired watches, runs one
// session at a time (time sync, link-status notification, alert notifications) and keeps the
// per-watch alert queue. The BlueZ calls are behind `BluezAdapter` so the logic is tested on the PC
// with a fake; the real adapter is in bluezadapter.h. Requirements: R-TIME-6, R-TIME-8, R-TIME-10,
// R-TIME-11, R-TIME-12, R-NOTIF-5, R-NOTIF-9, R-NOTIF-10, R-NOTIF-11, R-GEN-5;
#ifndef WATCHLINK_H
#define WATCHLINK_H

#include "modeltable.h"
#include "notiffilter.h"
#include "scheduler.h"
#include "settings.h"
#include "watchproto.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace watchlink {

// ---------------------------------------------------------------- constants

// [proposed starting values, tuned on the phone] connect attempt of one
// watch, wait for ServicesResolved after the connection, answer to a feature
// request, pause before the disconnect so that frames written without
// response leave the controller.
const int kConnectTimeoutMs = 20000;
const int kServicesTimeoutMs = 10000;
const int kFeatureTimeoutMs = 2000;
const int kSettleMs = 500;
// Pause between two writes to 0030 (also status -> first alert). Sending one frame every 0.6 s
// delivered all 8; 8 frames 3 ms apart delivered only 2, so the watch needs pacing.
const int kWriteGapMs = 600;

// Queue per watch, in memory only, circular: at most 10 waiting, when full the oldest goes. An item
// older than 5 minutes is dropped, never sent late (an assumption, not measured).
const int kMaxQueued = 10;
const int kMaxAgeSeconds = 5 * 60;

// When no connection could be made the alerts stay queued and the watch is tried again after
// increasing intervals: 30 s, 1 min, 2 min, then 4 min. 0 failures -> 0. (30 s is also the shortest
// wake-up sleep.)
int retryDelaySeconds(int consecutiveFailures);

// GATT: the vendor characteristics.
extern const char *const kUuidRequest;   // 26eb002c, write without response
extern const char *const kUuidData;      // 26eb002d, write + notify
extern const char *const kUuidNotif;     // 26eb0030, write without response

// Feature ids used here.
const quint8 kFeatureTime = 0x09;
const quint8 kFeatureBleInfo = 0x10;

// ------------------------------------------------------------------ adapter

// One BlueZ device as the adapter sees it.
struct BluezDevice {
    QString path;       // object path (BlueZ may recreate it)
    QString address;    // Device1.Address
    QString name;       // Device1.Name (not the user-editable Alias)
    QString alias;      // Device1.Alias
    bool paired;
    bool connected;
    BluezDevice() : paired(false), connected(false) {}
};

// Blocking calls: each returns true on success or false with a short text in
// *error (BlueZ's message; never notification content). The adapter does no
// LE discovery (R-TIME-11).
class BluezAdapter
{
public:
    virtual ~BluezAdapter() {}

    // All devices BlueZ knows (paired or not).
    virtual bool devices(QList<BluezDevice> *out, QString *error) = 0;
    // Device1.Connect; on timeout it cancels the attempt and fails.
    virtual bool connectDevice(const QString &path, int timeoutMs, QString *error) = 0;
    virtual bool waitServicesResolved(const QString &path, int timeoutMs, QString *error) = 0;
    // GattCharacteristic1.StartNotify on the characteristic with this UUID.
    virtual bool startNotify(const QString &path, const QString &uuid, QString *error) = 0;
    // WriteValue with response (type "request") or without ("command").
    virtual bool write(const QString &path, const QString &uuid, const QByteArray &value,
                       bool withResponse, QString *error) = 0;
    // Writes the 1-byte feature id to 002c and waits for the answer on 002d
    // (first byte = feature id). StartNotify on 002d must have been called.
    virtual bool readFeature(const QString &path, quint8 feature, int timeoutMs,
                             QByteArray *answer, QString *error) = 0;
    // Errors are ignored: the connection may be gone already.
    virtual void disconnectDevice(const QString &path) = 0;
    // Lets `ms` pass (event loop kept alive in the real adapter).
    virtual void settle(int ms) = 0;
};

// Receives one line per step of a session (metadata only, no notification text).
class LogSink
{
public:
    virtual ~LogSink() {}
    virtual void log(const QString &line) = 0;
};

// ------------------------------------------------------------------ results

// A paired watch found in BlueZ whose name matches the model table.
struct FoundWatch {
    BluezDevice device;
    const watchproto::ModelInfo *model;   // valid while the ModelTable lives
    FoundWatch() : model(0) {}
};

struct SessionRequest {
    settings::WatchEntry watch;
    bool timeSync;                 // do a time sync in this session
    QString expectedBleAddress;    // stored from an earlier feature 0x10; empty if unknown
    bool readBleAddress;           // read 0x10 even if nothing is expected (first connection)
    SessionRequest() : timeSync(false), readBleAddress(false) {}
};

struct SessionResult {
    QString key;             // WatchEntry::key
    bool skipped;            // nothing queued and no sync: no connection was made
    bool connected;
    bool timeSent;           // the time frame was written (the best success the watch allows)
    int alertsSent;
    int alertsDropped;       // lost: aged, queue full, or the session failed after the connection
    int alertsKept;          // no connection: kept in memory for the next attempt
    QDateTime nextAttempt;   // when alerts-only sessions are tried again (invalid if nothing waits)
    QString error;           // empty on success
    QDateTime when;          // phone time at the start of the session
    QString deviceAddress;   // BlueZ address used (the entry's may be outdated)
    QString bleAddress;      // from feature 0x10 when read, "AA:BB:CC:DD:EE:FF", else empty
    SessionResult()
        : skipped(false), connected(false), timeSent(false), alertsSent(0), alertsDropped(0), alertsKept(0) {}
    bool ok() const { return error.isEmpty(); }
};

// ----------------------------------------------------------------- WatchLink

class WatchLink
{
    Q_DECLARE_TR_FUNCTIONS(WatchLink)
public:
    // None of the pointers is owned; all must outlive the object.
    WatchLink(BluezAdapter *adapter, const watchproto::ModelTable *models,
              const scheduler::ClockSource *clock);

    void setLogSink(LogSink *sink) { m_log = sink; }
    // Pause between notification writes; 0 = none (tests, tuning).
    void setWriteGapMs(int ms) { m_gapMs = ms; }

    // Paired devices whose Name matches the model table (R-GEN-3, R-TIME-11:
    // from BlueZ's object tree, no discovery), in BlueZ's order.
    QList<FoundWatch> findWatches(QString *error);

    // ---- queue (per watch key)
    // Adds the alert to the queue of every enabled watch of `targets`
    // (R-GEN-5). The alert flag is always set (R-NOTIF-10). When a queue is
    // full the oldest item is dropped. Returns the number of items dropped.
    int enqueueAlert(const notiffilter::WatchNotification &n,
                     const QList<settings::WatchEntry> &targets);
    int queued(const QString &watchKey) const;
    // Earliest time an alerts-only session is tried for this watch (invalid
    // if the last connection worked or nothing failed). Time-sync sessions
    // are never held back here: their back-off is the scheduler's.
    QDateTime nextAttempt(const QString &watchKey) const;
    void clearQueue(const QString &watchKey);

    // ---- sessions (blocking; the service runs them off the GUI thread, or accepts that the event
    // loop spins inside the adapter)
    // One session for one watch: drains its queue. With nothing queued and no time sync it does not connect (skipped). With alerts only and a
    // retry time in the future it does not connect either (skipped). A second call while a session
    // runs fails at once with "busy".
    SessionResult runSession(const SessionRequest &request);
    // Sessions one after another in list order, never at the same time
    // (R-GEN-5); disabled entries are skipped.
    QList<SessionResult> runAll(const QList<SessionRequest> &requests);

    // "AA:BB:CC:DD:EE:FF" from the answer to feature 0x10 (bytes 1-6, reversed), or an empty string
    // if the answer is not a 0x10 answer.
    static QString bleAddressFromFeature10(const QByteArray &answer);

private:
    struct QueuedAlert {
        notiffilter::WatchNotification n;
        QDateTime enqueued;
    };

    void say(const QString &line);
    watchproto::DateTime phoneTime(QDateTime *now) const;
    bool findDevice(const settings::WatchEntry &entry, QString *error, FoundWatch *out);
    bool buildNotifFrame(const watchproto::Notification &n, QByteArray *wire, QString *error) const;
    SessionResult session(const SessionRequest &request, QList<QueuedAlert> alerts);

    BluezAdapter *m_adapter;
    const watchproto::ModelTable *m_models;
    const scheduler::ClockSource *m_clock;
    LogSink *m_log;
    QMap<QString, QList<QueuedAlert> > m_queues;
    QMap<QString, int> m_failures;          // consecutive failed connections, per watch
    QMap<QString, QDateTime> m_nextAttempt;
    quint32 m_nextId;
    bool m_busy;
    int m_gapMs;
    QElapsedTimer m_since;   // start of the running session, for the log
};

} // namespace watchlink

#endif // WATCHLINK_H
