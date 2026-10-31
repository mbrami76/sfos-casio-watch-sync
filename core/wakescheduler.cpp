// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "wakescheduler.h"

#ifdef HAVE_KEEPALIVE
#include <keepalive/backgroundactivity.h>
#endif

namespace scheduler {

BackgroundActivityWake::BackgroundActivityWake(const ClockSource *clock, QObject *parent)
    : WakeScheduler(parent), m_clock(clock), m_maxSleep(kDefaultMaxSleepSeconds),
      m_scheduled(false), m_holds(0), m_activity(0), m_holdActivity(0)
{
    m_range.minSec = m_range.maxSec = 0;
#ifdef HAVE_KEEPALIVE
    m_activity = new BackgroundActivity(this);
    connect(m_activity, SIGNAL(stateChanged()), this, SLOT(onStateChanged()));
    m_holdActivity = new BackgroundActivity(this);
#endif
}

BackgroundActivityWake::~BackgroundActivityWake()
{
}

void BackgroundActivityWake::setMaxSleepSeconds(int seconds)
{
    m_maxSleep = seconds < kMinSleepSeconds ? kMinSleepSeconds : seconds;
}

void BackgroundActivityWake::schedule(const QDateTime &at)
{
    // A request made while the activity still waits does not replace the earlier range (found on
    // the phone): stop it first.
#ifdef HAVE_KEEPALIVE
    m_activity->stop();
#endif
    const qint64 delay = (at.toMSecsSinceEpoch() - m_clock->now().toMSecsSinceEpoch()) / 1000;
    m_range = wakeRangeFor(delay, m_maxSleep);
    m_scheduled = true;
#ifdef HAVE_KEEPALIVE
    m_activity->wait(m_range.minSec, m_range.maxSec);
#endif
}

void BackgroundActivityWake::cancel()
{
    m_scheduled = false;
#ifdef HAVE_KEEPALIVE
    m_activity->stop();
#endif
}

void BackgroundActivityWake::hold()
{
    if (++m_holds == 1) {
#ifdef HAVE_KEEPALIVE
        m_holdActivity->run();
#endif
    }
}

void BackgroundActivityWake::release()
{
    if (m_holds > 0 && --m_holds == 0) {
#ifdef HAVE_KEEPALIVE
        m_holdActivity->stop();
#endif
    }
}

bool BackgroundActivityWake::isScheduled() const
{
#ifdef HAVE_KEEPALIVE
    return m_scheduled && m_activity->isWaiting();
#else
    return m_scheduled;
#endif
}

void BackgroundActivityWake::onStateChanged()
{
#ifdef HAVE_KEEPALIVE
    // Waiting -> Running is the wake-up. The activity stays Running (the
    // process is kept awake) until the next schedule() or cancel().
    if (m_scheduled && m_activity->isRunning()) {
        m_scheduled = false;
        emit wake();
    }
#endif
}

bool BackgroundActivityWake::simulateWake()
{
#ifdef HAVE_KEEPALIVE
    return false;
#else
    if (!m_scheduled)
        return false;
    m_scheduled = false;
    emit wake();
    return true;
#endif
}

} // namespace scheduler
