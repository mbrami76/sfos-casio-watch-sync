// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "workerrunner.h"

#include "bluezadapter.h"
#include "servicelog.h"

namespace service {

namespace {

class DebugSink : public watchlink::LogSink
{
public:
    void log(const QString &line) { debugLog(QStringLiteral("link  ") + line); }
};

} // namespace

// Lives in the worker thread. Everything below runs there.
class BlueWorker : public QObject
{
    Q_OBJECT
public:
    BlueWorker(const watchproto::ModelTable *models, const scheduler::ClockSource *clock)
        : m_models(models), m_clock(clock), m_adapter(0), m_link(0) {}
    ~BlueWorker()
    {
        delete m_link;
        delete m_adapter;
    }

public slots:
    void init()
    {
        m_adapter = new watchlink::BluezDBusAdapter;
        m_link = new watchlink::WatchLink(m_adapter, m_models, m_clock);
        m_link->setLogSink(&m_sink);
    }

    void run(const service::SessionJob &job)
    {
        for (int i = 0; i < job.clearKeys.size(); ++i)
            m_link->clearQueue(job.clearKeys.at(i));
        for (int i = 0; i < job.alerts.size(); ++i)
            m_link->enqueueAlert(job.alerts.at(i), job.alertTargets);
        const QList<watchlink::SessionResult> results = m_link->runAll(job.requests);
        emit finished(results);
    }

    void list()
    {
        QString error;
        const QList<watchlink::FoundWatch> found = m_link->findWatches(&error);
        QList<FoundDevice> out;
        for (int i = 0; i < found.size(); ++i) {
            FoundDevice d;
            d.address = found.at(i).device.address;
            d.name = found.at(i).device.name;
            d.alias = found.at(i).device.alias;
            d.connected = found.at(i).device.connected;
            if (found.at(i).model) {
                d.modelId = found.at(i).model->id;
                d.modelLabel = found.at(i).model->label;
                d.supported = found.at(i).model->status == watchproto::ModelSupported;
            }
            out.append(d);
        }
        emit listed(out, error);
    }

signals:
    void finished(const QList<watchlink::SessionResult> &results);
    void listed(const QList<service::FoundDevice> &devices, const QString &error);

private:
    const watchproto::ModelTable *m_models;
    const scheduler::ClockSource *m_clock;
    watchlink::BluezDBusAdapter *m_adapter;
    watchlink::WatchLink *m_link;
    DebugSink m_sink;
};

WorkerRunner::WorkerRunner(const watchproto::ModelTable *models,
                           const scheduler::ClockSource *clock, QObject *parent)
    : SessionRunner(parent), m_models(models), m_clock(clock), m_worker(0)
{
    qRegisterMetaType<service::SessionJob>("service::SessionJob");
    qRegisterMetaType<QList<watchlink::SessionResult> >("QList<watchlink::SessionResult>");
    qRegisterMetaType<QList<service::FoundDevice> >("QList<service::FoundDevice>");
}

WorkerRunner::~WorkerRunner()
{
    // A session in progress ends first (the loop of the thread is left when
    // the slot returns); the objects of the worker are deleted afterwards.
    m_thread.quit();
    m_thread.wait();
    delete m_worker;
}

void WorkerRunner::start()
{
    if (m_worker)
        return;
    m_worker = new BlueWorker(m_models, m_clock);
    m_worker->moveToThread(&m_thread);
    connect(m_worker, SIGNAL(finished(QList<watchlink::SessionResult>)), this,
            SIGNAL(jobFinished(QList<watchlink::SessionResult>)));
    connect(m_worker, SIGNAL(listed(QList<service::FoundDevice>, QString)), this,
            SIGNAL(watchesListed(QList<service::FoundDevice>, QString)));
    m_thread.start();
    QMetaObject::invokeMethod(m_worker, "init", Qt::QueuedConnection);
}

void WorkerRunner::submit(const SessionJob &job)
{
    QMetaObject::invokeMethod(m_worker, "run", Qt::QueuedConnection,
                              Q_ARG(service::SessionJob, job));
}

void WorkerRunner::listWatches()
{
    QMetaObject::invokeMethod(m_worker, "list", Qt::QueuedConnection);
}

} // namespace service

#include "workerrunner.moc"
