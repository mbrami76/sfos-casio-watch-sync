// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// workerrunner: the real SessionRunner. One dedicated thread owns the BluezDBusAdapter and the
// WatchLink: the sessions block, and the adapter spins nested event loops while it waits, none of
// which may happen in the main thread that serves the D-Bus API and the monitor. The worker uses
// the system bus (org.bluez); the main thread uses only the session bus. The WatchLink queues live
// in the worker and are touched only there.
#ifndef WORKERRUNNER_H
#define WORKERRUNNER_H

#include "scheduler.h"
#include "serviceiface.h"
#include "watchlink.h"

#include <QThread>

namespace service {

class BlueWorker;

class WorkerRunner : public SessionRunner
{
    Q_OBJECT
public:
    // `models` and `clock` must outlive the object; the table is only read.
    WorkerRunner(const watchproto::ModelTable *models, const scheduler::ClockSource *clock,
                 QObject *parent = 0);
    ~WorkerRunner();

    void start();
    void submit(const SessionJob &job);
    void listWatches();

private:
    const watchproto::ModelTable *m_models;
    const scheduler::ClockSource *m_clock;
    QThread m_thread;
    BlueWorker *m_worker;
};

} // namespace service

#endif // WORKERRUNNER_H
