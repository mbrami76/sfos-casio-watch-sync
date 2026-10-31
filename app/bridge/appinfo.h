// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Facts for the About page and links to the system.
#ifndef APPINFO_H
#define APPINFO_H

#include <QObject>
#include <QString>

namespace uibridge {

// The project address. A placeholder until packaging decides how the package metadata reaches the
// program; the only place that returns it.
QString projectAddress();

class AppInfo : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString shortName READ shortName CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString license READ license CONSTANT)
    Q_PROPERTY(QString address READ address CONSTANT)
public:
    explicit AppInfo(QObject *parent = 0) : QObject(parent) {}
    QString title() const { return QStringLiteral("Casio Watch Sync"); }
    QString shortName() const { return QStringLiteral("CWS"); }
    QString version() const;
    QString license() const { return QStringLiteral("GPL-3.0-or-later"); }
    QString address() const { return projectAddress(); }

    // Asks the Sailfish settings app to show its Bluetooth page (session bus,
    // com.jolla.settings.ui.showPage). Fire and forget; false if the call
    // could not be sent.
    Q_INVOKABLE bool openBluetoothSettings() const;
};

} // namespace uibridge

#endif // APPINFO_H
