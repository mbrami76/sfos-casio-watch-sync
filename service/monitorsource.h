// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// The real NotifSource: a NotifMonitor (core/notifmonitor.h) in the main thread.
#ifndef MONITORSOURCE_H
#define MONITORSOURCE_H

#include "notifmonitor.h"
#include "serviceiface.h"

namespace service {

class MonitorSource : public NotifSource
{
    Q_OBJECT
public:
    explicit MonitorSource(QObject *parent = 0);
    void start() { m_monitor.start(); }
    void stop() { m_monitor.stop(); }
    int state() const { return m_monitor.state(); }
    QString reason() const { return m_monitor.reason(); }
    void setDebugLogging(bool on) { m_monitor.setDebugLogging(on); }

private:
    notifmonitor::NotifMonitor m_monitor;
};

} // namespace service

#endif // MONITORSOURCE_H
