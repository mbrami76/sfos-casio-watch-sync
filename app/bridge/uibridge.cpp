// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "uibridge.h"

namespace uibridge {

UiPaths::UiPaths()
    : settingsPath(settings::defaultSettingsPath()),
      statePath(settings::defaultStatePath()),
      uiPrefsPath(SettingsBridge::defaultUiPrefsPath()),
      modelsPath(settings::installedModelsPath()),
      silhouettesDir(SilhouetteCatalog::installedDir())
{
}

static const watchproto::ModelTable &loadedModels(watchproto::ModelTable *t, const UiPaths &p)
{
    settings::loadModelTable(t, p.modelsPath, p.modelsFallbackPath);
    return *t;
}

UiBridge::UiBridge(const UiPaths &paths, const QDBusConnection &bus, QObject *parent)
    : QObject(parent),
      m_silhouettes(paths.silhouettesDir),
      m_settings(paths.settingsPath, paths.uiPrefsPath),
      m_client(bus),
      m_watches(m_settings.settings(), &loadedModels(&m_models, paths), &m_silhouettes,
                paths.statePath),
      m_enabled(&m_watches),
      m_apps(m_settings.settings())
{
    connect(&m_settings, SIGNAL(written()), &m_client, SLOT(reloadSettings()));
    connect(&m_watches, SIGNAL(written()), &m_client, SLOT(reloadSettings()));
    connect(&m_apps, SIGNAL(written()), &m_client, SLOT(reloadSettings()));
    connect(&m_client, SIGNAL(statusChanged()), this, SLOT(onStatus()));
    connect(&m_client, SIGNAL(listingChanged()), this, SLOT(onListing()));
    connect(&m_watches, SIGNAL(rebuilt()), this, SLOT(updateLastSyncLine()));
    updateLastSyncLine();
    if (m_client.available())
        m_client.refresh();
}

void UiBridge::onStatus()
{
    // The service writes the settings file too (discovered apps): read it first.
    m_settings.reload();
    m_watches.setServiceStatus(m_client.status().watches, m_client.statusKnown());
}

void UiBridge::onListing()
{
    if (!m_client.listing() && m_client.listError().isEmpty())
        m_watches.setListed(m_client.listedWatches());
}

void UiBridge::updateLastSyncLine()
{
    const QString line = Formatter::lastSyncLine(m_watches.anyUsable(),
                                                 m_watches.oldestLastSuccess(),
                                                 QDateTime::currentDateTime());
    if (line != m_lastSyncLine) {
        m_lastSyncLine = line;
        emit lastSyncLineChanged();
    }
}

void UiBridge::refresh()
{
    m_settings.reload();
    if (m_client.available())
        m_client.refresh();
    else
        m_watches.rebuild();
}

} // namespace uibridge
