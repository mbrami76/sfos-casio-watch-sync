// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// notiffilter: pure rules deciding whether a phone notification goes to the watch and what the
// watch notification looks like. No I/O, no clock, no state between calls. Requirements: R-NOTIF-1,
// R-NOTIF-4, R-NOTIF-10, R-NOTIF-11;
#ifndef NOTIFFILTER_H
#define NOTIFFILTER_H

#include "watchproto.h"

#include <QDateTime>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace notiffilter {

// What notifmonitor extracts from a Notify call. `when` is informational: the
// filter never reads a clock and does not use it.
struct IncomingNotification {
    QString appName;
    QString summary;
    QString body;
    uint replacesId;
    QVariantMap hints;
    QDateTime when;
    IncomingNotification() : replacesId(0) {}
};

// The watchproto::Notification fields without `id` and `timestamp`; the sender
// (watchlink/service) adds both when it writes. UTF-8, as in watchproto.
struct WatchNotification {
    bool alert;
    uint8_t category;
    std::string sender, title, subtitle, message;
    WatchNotification() : alert(false), category(0) {}
};

struct NotifSettings {
    bool masterOn;
    QSet<QString> enabledApps;           // exact app_name, as listed by settings
    QStringList blacklist;               // substrings, see isBlacklisted()
    QMap<QString, int> categoryByApp;    // app_name (case-insensitive) -> watch category
    QString ownAppName;                  // app_name of this application's own notifications
    NotifSettings() : masterOn(false) {}
};

enum Decision {
    Forward = 0,
    DropMasterOff,
    DropOwnApp,
    DropBlacklisted,
    DropPrivate,
    DropEmpty,
    DropNotEnabled
};

struct FilterResult {
    Decision decision;
    bool discoverApp;           // true only with DropNotEnabled: settings should list appName (disabled)
    WatchNotification n;        // valid only when decision == Forward
    FilterResult() : decision(DropMasterOff), discoverApp(false) {}
    bool forward() const { return decision == Forward; }
};

// Sender policy (R-NOTIF-11 note: the four fields share 184 B): whitespace
// runs, newlines and tabs become one space, ends trimmed, then at most
// kMaxSenderChars characters and kMaxSenderBytes bytes, cut on a character
// boundary. [proposed values]
const int kMaxSenderChars = 20;
const int kMaxSenderBytes = 40;
std::string shortenSender(const QString &appName);

// Decision order: master off; own app; blacklisted (never discovered);
// x-nemo-visibility = private; empty summary and body; app not in enabledApps
// (dropped, discoverApp set); otherwise forward. Forwarded: sender =
// shortenSender(app), title = summary, subtitle empty, message = body, alert
// set (R-NOTIF-10), category from categoryByApp (default 0).
FilterResult filter(const IncomingNotification &in, const NotifSettings &s);

// Case-insensitive substring of appName; entries are trimmed and empty ones are ignored; no
// wildcards, no regular expressions.
bool isBlacklisted(const QString &appName, const QStringList &blacklist);

// Watch category for an app: case-insensitive lookup, values outside
// 0..watchproto::kMaxCategory count as 0; unknown app -> 0.
int categoryFor(const QString &appName, const QMap<QString, int> &table);

// Default app_name -> category table. Code, not user data.
QMap<QString, int> defaultCategoryTable();

} // namespace notiffilter

#endif // NOTIFFILTER_H
