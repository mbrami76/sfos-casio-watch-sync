// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "servicemain.h"

#include "monitorsource.h"
#include "nemopublisher.h"
#include "servicebus.h"
#include "servicecore.h"
#include "servicelog.h"
#include "wakescheduler.h"
#include "workerrunner.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QSocketNotifier>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

namespace service {

namespace {

int g_signalFd[2] = { -1, -1 };

void onUnixSignal(int)
{
    const char c = 1;
    ssize_t r = ::write(g_signalFd[0], &c, 1);
    (void)r;
}

// SIGINT/SIGTERM end the event loop (a clean exit; the resident notification stays).
class QuitOnSignal : public QObject
{
    Q_OBJECT
public:
    QuitOnSignal() : m_notifier(0)
    {
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalFd) != 0)
            return;
        m_notifier = new QSocketNotifier(g_signalFd[1], QSocketNotifier::Read, this);
        connect(m_notifier, SIGNAL(activated(int)), this, SLOT(onActivated()));
        struct sigaction sa;
        sa.sa_handler = onUnixSignal;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_RESTART;
        sigaction(SIGINT, &sa, 0);
        sigaction(SIGTERM, &sa, 0);
    }
private slots:
    void onActivated()
    {
        char c;
        ssize_t r = ::read(g_signalFd[1], &c, 1);
        (void)r;
        QCoreApplication::quit();
    }
private:
    QSocketNotifier *m_notifier;
};

} // namespace

int runService(const QString &modelsFile)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        fprintf(stderr, "sfos-casio-sync: no session bus\n");
        return 1;
    }

    // Everything below is cheap or inert until the name is ours.
    settings::Settings settings;
    settings::State state;
    watchproto::ModelTable models;
    QString modelsError;
    if (!settings::loadModelTable(&models, modelsFile.isEmpty() ? settings::installedModelsPath()
                                                                : modelsFile,
                                  modelsFile.isEmpty() ? QString() : settings::installedModelsPath(),
                                  &modelsError))
        fprintf(stderr, "sfos-casio-sync: model table not loaded: %s\n", qPrintable(modelsError));

    scheduler::SystemClock clock;
    scheduler::BackgroundActivityWake wake(&clock);
    NemoPublisher publisher;
    MonitorSource monitor;
    WorkerRunner runner(&models, &clock);

    ServiceDeps deps;
    deps.settings = &settings;
    deps.state = &state;
    deps.models = &models;
    deps.clock = &clock;
    deps.wake = &wake;
    deps.runner = &runner;
    deps.monitor = &monitor;
    deps.publisher = &publisher;
    ServiceCore core(deps);

    ServiceObject holder;
    if (!registerOnBus(bus, &core, &holder)) {
        fprintf(stderr, "sfos-casio-sync: the service name is taken (another instance runs)\n");
        return 1;
    }

    QuitOnSignal quitter;
    runner.start();
    core.start();
    const int rc = QCoreApplication::exec();
    debugLog(QStringLiteral("service stopped"));
    return rc;
}

} // namespace service

#include "servicemain.moc"
