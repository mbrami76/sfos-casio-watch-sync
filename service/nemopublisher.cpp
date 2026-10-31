// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "nemopublisher.h"

#include "notifmonitor.h"
#include "servicebus.h"

#include <notification.h>

#include <QVariantList>
#include <QVariantMap>

namespace service {

namespace {
const char kCategory[] = "x-sfos-casio-sync.status";
}

NemoPublisher::NemoPublisher() : m_notification(0)
{
}

void NemoPublisher::ensure()
{
    if (m_notification)
        return;
    m_notification = new Notification;
    m_notification->setAppName(notifmonitor::ownAppName());
    m_notification->setCategory(QLatin1String(kCategory));
    m_notification->setIsTransient(false);
    m_notification->setResident(true);     // stays after a tap

    QVariantMap action;
    action.insert(QStringLiteral("name"), QStringLiteral("default"));
    action.insert(QStringLiteral("service"), QLatin1String(kBusName));
    action.insert(QStringLiteral("path"), QLatin1String(kObjectPath));
    action.insert(QStringLiteral("iface"), QLatin1String(kInterfaceName));
    action.insert(QStringLiteral("method"), QStringLiteral("OpenApp"));
    m_notification->setRemoteAction(action);

    // A notification of an earlier run survives: replace it, do not add a second one.
    const QList<QObject *> old = Notification::notifications();
    for (int i = 0; i < old.size(); ++i) {
        Notification *n = qobject_cast<Notification *>(old.at(i));
        if (n && n->category() == QLatin1String(kCategory) && !m_notification->replacesId())
            m_notification->setReplacesId(n->replacesId());
        delete old.at(i);
    }
}

NemoPublisher::~NemoPublisher()
{
    delete m_notification;      // leaves the published notification in place
}

void NemoPublisher::publish(const QString &summary, const QString &body)
{
    ensure();
    m_notification->setSummary(summary);
    m_notification->setBody(body);
    // No banner for an update of a status line.
    m_notification->setPreviewSummary(QString());
    m_notification->setPreviewBody(QString());
    m_notification->publish();
}

} // namespace service
