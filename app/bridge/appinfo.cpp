// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "appinfo.h"

#include <QDBusConnection>
#include <QDBusMessage>

namespace uibridge {

QString projectAddress()
{
    return QStringLiteral(CWS_URL);   // from bridge.pri / the package URL
}

// CWS_VERSION comes from bridge.pri (the package version is the packager's).
QString AppInfo::version() const
{
    return QStringLiteral(CWS_VERSION);
}

bool AppInfo::openBluetoothSettings() const
{
    QDBusMessage m = QDBusMessage::createMethodCall(
        QStringLiteral("com.jolla.settings"), QStringLiteral("/com/jolla/settings/ui"),
        QStringLiteral("com.jolla.settings.ui"), QStringLiteral("showPage"));
    m << QStringLiteral("system_settings/connectivity/bluetooth");
    // D-Bus activation starts the settings app if it is not running.
    return QDBusConnection::sessionBus().send(m);
}

} // namespace uibridge
