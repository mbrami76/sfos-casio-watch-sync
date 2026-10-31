// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Wake-up wrapper: keeps the process waking up while the phone sleeps, and keeps it
// awake on request (hold/release). Plain Qt timers do not run in suspend, so the device build uses
// libkeepalive's BackgroundActivity (C++ API); the host build is a stub with the same behaviour
// towards the caller and a test hook, so the logic around it can be tested on the PC.
#ifndef WAKESCHEDULER_H
#define WAKESCHEDULER_H

#include "scheduler.h"

#include <QObject>

class BackgroundActivity;

namespace scheduler {

// Interface used by the service; tests use their own fake.
class WakeScheduler : public QObject
{
    Q_OBJECT
public:
    explicit WakeScheduler(QObject *parent = 0) : QObject(parent) {}
    // Ask for one wake-up at about `at`. A pending request is replaced, whether the new time is
    // sooner or later. The process stays active after wake() until schedule() or cancel() is
    // called.
    virtual void schedule(const QDateTime &at) = 0;
    virtual void cancel() = 0;
    virtual bool isScheduled() const = 0;
    // Keep the phone awake, e.g. during a BLE session. Counted: every hold() needs a release(); the
    // phone may sleep again when the count is 0. Independent of schedule()/cancel(). An extra
    // release() is ignored.
    virtual void hold() = 0;
    virtual void release() = 0;
signals:
    void wake();
};

// Device build (HAVE_KEEPALIVE): BackgroundActivity::wait(min, max).
// Host build: records the request; simulateWake() stands for the system.
class BackgroundActivityWake : public WakeScheduler
{
    Q_OBJECT
public:
    // `clock` must outlive the object (not owned).
    explicit BackgroundActivityWake(const ClockSource *clock, QObject *parent = 0);
    ~BackgroundActivityWake();

    // Longest sleep per request (default kDefaultMaxSleepSeconds). A later
    // time is reached in several steps: on each wake the service calls decide().
    void setMaxSleepSeconds(int seconds);
    int maxSleepSeconds() const { return m_maxSleep; }

    void schedule(const QDateTime &at);
    void cancel();
    bool isScheduled() const;
    void hold();
    void release();
    // Number of open holds.
    int holdCount() const { return m_holds; }

    // The range handed to the system by the last schedule().
    WakeRange lastRange() const { return m_range; }

    // Host stub only (no effect on the device build): act as the system waking
    // the process. Returns false if nothing was scheduled.
    bool simulateWake();

private slots:
    void onStateChanged();

private:
    const ClockSource *m_clock;
    int m_maxSleep;
    WakeRange m_range;
    bool m_scheduled;
    int m_holds;
    BackgroundActivity *m_activity;       // wake-ups; 0 in the host stub
    BackgroundActivity *m_holdActivity;   // Running while m_holds > 0; 0 in the host stub
};

} // namespace scheduler

#endif // WAKESCHEDULER_H
