// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// The real BluezAdapter: org.bluez on the system bus over QtDBus (no QtBluetooth, R-PLAT-2/3). Thin
// by design; the logic is in watchlink.h. Not
// covered by host tests (no BlueZ on the PC); exercised by watchlink-cli on the phone. Needs an
// event loop (it spins a local one while it waits).
#ifndef BLUEZADAPTER_H
#define BLUEZADAPTER_H

#include "watchlink.h"

#include <QEventLoop>
#include <QMap>
#include <QObject>
#include <QVariantMap>

namespace watchlink {

class BluezDBusAdapter : public QObject, public BluezAdapter
{
    Q_OBJECT
public:
    explicit BluezDBusAdapter(QObject *parent = 0);

    bool devices(QList<BluezDevice> *out, QString *error);
    bool connectDevice(const QString &path, int timeoutMs, QString *error);
    bool waitServicesResolved(const QString &path, int timeoutMs, QString *error);
    bool startNotify(const QString &path, const QString &uuid, QString *error);
    bool write(const QString &path, const QString &uuid, const QByteArray &value,
               bool withResponse, QString *error);
    bool readFeature(const QString &path, quint8 feature, int timeoutMs, QByteArray *answer,
                     QString *error);
    void disconnectDevice(const QString &path);
    void settle(int ms);

private slots:
    void onPropertiesChanged(const QString &iface, const QVariantMap &changed,
                             const QStringList &invalidated);

private:
    QString characteristicPath(const QString &devicePath, const QString &uuid, QString *error);
    bool deviceProperty(const QString &path, const char *name, QVariant *value, QString *error);

    QMap<QString, QString> m_charCache;   // devicePath + uuid -> characteristic path
    // readFeature state
    quint8 m_wantFeature;
    bool m_gotAnswer;
    QByteArray m_answer;
    QEventLoop *m_loop;                   // loop of the running readFeature, or 0
};

} // namespace watchlink

#endif // BLUEZADAPTER_H
