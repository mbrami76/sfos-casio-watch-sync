// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Single instance of the UI The service's OpenApp starts the binary every time it is
// called, so the program must make itself unique. Mechanism: the UI owns a bus name of its own on
// the session bus (`it.michelebrami.sfos_casio_sync.ui`) with a method Activate(). A second start
// finds the name taken, calls Activate() on the running window and exits. The name disappears with
// the process, so there is no stale lock to clean up.
#ifndef INSTANCEGUARD_H
#define INSTANCEGUARD_H

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QObject>

namespace uibridge {

extern const char kUiBusName[];     // it.michelebrami.sfos_casio_sync.ui
extern const char kUiPath[];        // /it/michelebrami/sfos_casio_sync/ui
extern const char kUiInterface[];   // it.michelebrami.sfos_casio_sync.Ui

class InstanceGuard : public QObject
{
    Q_OBJECT
public:
    explicit InstanceGuard(const QDBusConnection &bus = QDBusConnection::sessionBus(),
                           QObject *parent = 0);

    // True: this process is the only UI instance and listens for Activate().
    // False: another instance owns the name; it was asked to come forward
    // (Activate() called, waits up to `timeoutMs`) and this process should exit.
    bool claim(int timeoutMs = 3000);

signals:
    void activateRequested();       // a second start asked to bring the window forward

private:
    QDBusConnection m_bus;
};

class InstanceAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "it.michelebrami.sfos_casio_sync.Ui")
public:
    explicit InstanceAdaptor(InstanceGuard *guard);
public slots:
    void Activate();
private:
    InstanceGuard *m_guard;
};

} // namespace uibridge

#endif // INSTANCEGUARD_H
