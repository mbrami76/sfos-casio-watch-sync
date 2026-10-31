// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Everything QML talks to, in one object (context property `cws`): settings,
// the service client, the list models, silhouettes, text helpers. Wires them:
// a write to the settings file is followed by ReloadSettings; a new status
// from the service refreshes the watch rows and re-reads the settings file
// (the service writes it too, for discovered apps).
#ifndef UIBRIDGE_H
#define UIBRIDGE_H

#include "appinfo.h"
#include "appsmodel.h"
#include "formatter.h"
#include "modeltable.h"
#include "serviceclient.h"
#include "settingsbridge.h"
#include "silhouettecatalog.h"
#include "watchesmodel.h"

#include <QDBusConnection>
#include <QObject>

namespace uibridge {

struct UiPaths {
    QString settingsPath;
    QString statePath;
    QString uiPrefsPath;
    QString modelsPath;             // installed models.json
    QString modelsFallbackPath;     // for development (--models)
    QString silhouettesDir;
    UiPaths();                      // the real locations
};

class UiBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject *service READ service CONSTANT)
    Q_PROPERTY(QObject *settings READ settings CONSTANT)
    Q_PROPERTY(QObject *watches READ watches CONSTANT)
    Q_PROPERTY(QObject *enabledWatches READ enabledWatches CONSTANT)
    Q_PROPERTY(QObject *apps READ apps CONSTANT)
    Q_PROPERTY(QObject *silhouettes READ silhouettes CONSTANT)
    Q_PROPERTY(QObject *format READ format CONSTANT)
    Q_PROPERTY(QObject *about READ about CONSTANT)
    // "Last sync <time>" for the cover (oldest of the enabled watches).
    Q_PROPERTY(QString lastSyncLine READ lastSyncLine NOTIFY lastSyncLineChanged)
public:
    explicit UiBridge(const UiPaths &paths = UiPaths(),
                      const QDBusConnection &bus = QDBusConnection::sessionBus(),
                      QObject *parent = 0);

    ServiceClient *service() { return &m_client; }
    SettingsBridge *settings() { return &m_settings; }
    WatchesModel *watches() { return &m_watches; }
    EnabledWatchesModel *enabledWatches() { return &m_enabled; }
    AppsModel *apps() { return &m_apps; }
    SilhouetteCatalog *silhouettes() { return &m_silhouettes; }
    Formatter *format() { return &m_format; }
    AppInfo *about() { return &m_about; }
    QString lastSyncLine() const { return m_lastSyncLine; }

    // Page opened or window active again: re-read the file and the status.
    Q_INVOKABLE void refresh();

signals:
    void lastSyncLineChanged();

private slots:
    void onStatus();
    void onListing();
    void updateLastSyncLine();

private:
    watchproto::ModelTable m_models;
    SilhouetteCatalog m_silhouettes;
    SettingsBridge m_settings;
    ServiceClient m_client;
    WatchesModel m_watches;
    EnabledWatchesModel m_enabled;
    AppsModel m_apps;
    Formatter m_format;
    AppInfo m_about;
    QString m_lastSyncLine;
};

} // namespace uibridge

#endif // UIBRIDGE_H
