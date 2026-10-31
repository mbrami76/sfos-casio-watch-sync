// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "notiffilter.h"

namespace notiffilter {

namespace {

bool equalsNoCase(const QString &a, const QString &b)
{
    return a.compare(b, Qt::CaseInsensitive) == 0;
}

std::string utf8(const QString &s)
{
    QByteArray b = s.toUtf8();
    return std::string(b.constData(), size_t(b.size()));
}

bool isPrivate(const QVariantMap &hints)
{
    QVariantMap::const_iterator it = hints.constFind(QStringLiteral("x-nemo-visibility"));
    if (it == hints.constEnd())
        return false;
    return it.value().toString().trimmed().compare(QStringLiteral("private"),
                                                   Qt::CaseInsensitive) == 0;
}

} // namespace

std::string shortenSender(const QString &appName)
{
    // QString::simplified() also folds newlines and tabs into single spaces.
    return watchproto::truncateUtf8(utf8(appName.simplified()),
                                    size_t(kMaxSenderChars), size_t(kMaxSenderBytes));
}

bool isBlacklisted(const QString &appName, const QStringList &blacklist)
{
    for (int i = 0; i < blacklist.size(); ++i) {
        const QString entry = blacklist.at(i).trimmed();
        if (entry.isEmpty())
            continue;
        if (appName.contains(entry, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

int categoryFor(const QString &appName, const QMap<QString, int> &table)
{
    for (QMap<QString, int>::const_iterator it = table.constBegin(); it != table.constEnd(); ++it) {
        if (equalsNoCase(it.key(), appName)) {
            int c = it.value();
            return (c >= 0 && c <= int(watchproto::kMaxCategory)) ? c : 0;
        }
    }
    return 0;
}

QMap<QString, int> defaultCategoryTable()
{
    // Empty by design: no app name is shipped, every app gets the generic category 0. An entry is
    // added here only after its real app_name has been seen. Verified icons for later entries: 4
    // chat bubble, 5 calendar, 6 envelope.
    return QMap<QString, int>();
}

FilterResult filter(const IncomingNotification &in, const NotifSettings &s)
{
    FilterResult r;
    if (!s.masterOn) {
        r.decision = DropMasterOff;
        return r;
    }
    if (!s.ownAppName.isEmpty() && equalsNoCase(in.appName.trimmed(), s.ownAppName.trimmed())) {
        r.decision = DropOwnApp;
        return r;
    }
    if (isBlacklisted(in.appName, s.blacklist)) {
        r.decision = DropBlacklisted;
        return r;
    }
    if (isPrivate(in.hints)) {
        r.decision = DropPrivate;
        return r;
    }
    if (in.summary.trimmed().isEmpty() && in.body.trimmed().isEmpty()) {
        r.decision = DropEmpty;
        return r;
    }
    if (!s.enabledApps.contains(in.appName)) {
        r.decision = DropNotEnabled;
        r.discoverApp = !in.appName.trimmed().isEmpty(); // nothing to list without a name
        return r;
    }
    r.decision = Forward;
    r.n.alert = true;
    r.n.category = uint8_t(categoryFor(in.appName, s.categoryByApp));
    r.n.sender = shortenSender(in.appName);
    r.n.title = utf8(in.summary);
    r.n.message = utf8(in.body);
    return r;
}

} // namespace notiffilter
