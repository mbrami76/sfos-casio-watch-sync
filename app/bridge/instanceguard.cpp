// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "instanceguard.h"

#include <QDBusMessage>

namespace uibridge {

const char kUiBusName[] = "it.michelebrami.sfos_casio_sync.ui";
const char kUiPath[] = "/it/michelebrami/sfos_casio_sync/ui";
const char kUiInterface[] = "it.michelebrami.sfos_casio_sync.Ui";

InstanceGuard::InstanceGuard(const QDBusConnection &bus, QObject *parent)
    : QObject(parent), m_bus(bus)
{
}

bool InstanceGuard::claim(int timeoutMs)
{
    if (!m_bus.isConnected())
        return true;        // no session bus: nothing to coordinate with, run
    new InstanceAdaptor(this);
    m_bus.registerObject(QLatin1String(kUiPath), this);
    // Not queued: if the name is taken, another instance runs.
    if (m_bus.registerService(QLatin1String(kUiBusName)))
        return true;
    QDBusMessage m = QDBusMessage::createMethodCall(
        QLatin1String(kUiBusName), QLatin1String(kUiPath), QLatin1String(kUiInterface),
        QStringLiteral("Activate"));
    m_bus.call(m, QDBus::Block, timeoutMs);
    return false;
}

InstanceAdaptor::InstanceAdaptor(InstanceGuard *guard) : QDBusAbstractAdaptor(guard), m_guard(guard)
{
}

void InstanceAdaptor::Activate()
{
    emit m_guard->activateRequested();
}

} // namespace uibridge
