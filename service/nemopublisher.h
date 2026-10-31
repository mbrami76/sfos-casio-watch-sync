// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// The real StatusPublisher: the resident notification of the app through
// nemonotifications. Published with notifmonitor::ownAppName() so the service's own filter drops
// it. Tapping it calls OpenApp() on the service's own D-Bus name: the D-Bus activation starts a
// dead service first, then the service opens the UI.
#ifndef NEMOPUBLISHER_H
#define NEMOPUBLISHER_H

#include "serviceiface.h"

class Notification;

namespace service {

class NemoPublisher : public StatusPublisher
{
public:
    NemoPublisher();
    ~NemoPublisher();
    void publish(const QString &summary, const QString &body);

private:
    void ensure();       // first publish: adopts the notification of an earlier run
    Notification *m_notification;
};

} // namespace service

#endif // NEMOPUBLISHER_H
