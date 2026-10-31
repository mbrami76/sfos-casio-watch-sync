// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "monitorsource.h"

namespace service {

MonitorSource::MonitorSource(QObject *parent) : NotifSource(parent)
{
    connect(&m_monitor, SIGNAL(notificationReceived(notiffilter::IncomingNotification)), this,
            SIGNAL(notificationReceived(notiffilter::IncomingNotification)));
    connect(&m_monitor, SIGNAL(stateChanged(int, QString)), this,
            SIGNAL(stateChanged(int, QString)));
}

} // namespace service
