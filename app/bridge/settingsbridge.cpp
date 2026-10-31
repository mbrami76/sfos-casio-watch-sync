// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "settingsbridge.h"

#include "formatter.h"

#include <QSettings>
#include <QStandardPaths>

namespace uibridge {

SettingsBridge::SettingsBridge(const QString &settingsPath, const QString &uiPrefsPath,
                               QObject *parent)
    : QObject(parent), m_settings(settingsPath), m_prefsPath(uiPrefsPath)
{
    m_master = m_settings.masterOn();
    m_interval = m_settings.intervalMinutes();
    m_debug = m_settings.debugLog();
    m_blacklist = m_settings.blacklist();
    connect(&m_settings, SIGNAL(changed()), this, SLOT(onSettingsChanged()));
}

QString SettingsBridge::defaultUiPrefsPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
           + QStringLiteral("/it.michelebrami/sfos-casio-sync-ui.conf");
}

void SettingsBridge::onSettingsChanged()
{
    if (m_master != m_settings.masterOn()) {
        m_master = m_settings.masterOn();
        emit masterOnChanged();
    }
    if (m_interval != m_settings.intervalMinutes()) {
        m_interval = m_settings.intervalMinutes();
        emit intervalChanged();
    }
    if (m_debug != m_settings.debugLog()) {
        m_debug = m_settings.debugLog();
        emit debugLogChanged();
    }
    if (m_blacklist != m_settings.blacklist()) {
        m_blacklist = m_settings.blacklist();
        emit blacklistChanged();
    }
}

void SettingsBridge::reload()
{
    m_settings.reload();
}

bool SettingsBridge::setMasterOn(bool on)
{
    m_settings.reload();
    if (!m_settings.setMasterOn(on))
        return false;
    emit written();
    return true;
}

bool SettingsBridge::setIntervalMinutes(int minutes)
{
    m_settings.reload();
    if (!m_settings.setIntervalMinutes(minutes))
        return false;
    emit written();
    return true;
}

bool SettingsBridge::setDebugLog(bool on)
{
    m_settings.reload();
    if (!m_settings.setDebugLog(on))
        return false;
    emit written();
    return true;
}

QVariantList SettingsBridge::intervalChoices() const
{
    QVariantList out;
    const QList<int> allowed = settings::allowedIntervalMinutes();
    for (int i = 0; i < allowed.size(); ++i) {
        QVariantMap m;
        m.insert(QStringLiteral("minutes"), allowed.at(i));
        m.insert(QStringLiteral("label"), Formatter::intervalLabel(allowed.at(i)));
        out.append(m);
    }
    return out;
}

int SettingsBridge::intervalIndex() const
{
    return settings::allowedIntervalMinutes().indexOf(m_settings.intervalMinutes());
}

bool SettingsBridge::addBlacklist(const QString &text)
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return false;
    m_settings.reload();
    QStringList list = m_settings.blacklist();
    for (int i = 0; i < list.size(); ++i)
        if (list.at(i).compare(t, Qt::CaseInsensitive) == 0)
            return false;
    list.append(t);
    if (!m_settings.setBlacklist(list))
        return false;
    emit written();
    return true;
}

bool SettingsBridge::removeBlacklist(int index)
{
    m_settings.reload();
    QStringList list = m_settings.blacklist();
    if (index < 0 || index >= list.size())
        return false;
    list.removeAt(index);
    if (!m_settings.setBlacklist(list))
        return false;
    emit written();
    return true;
}

bool SettingsBridge::notificationNoticeNeeded() const
{
    QSettings s(m_prefsPath, QSettings::IniFormat);
    return !s.value(QStringLiteral("notice/notificationReadingAccepted"), false).toBool();
}

void SettingsBridge::acceptNotificationNotice()
{
    QSettings s(m_prefsPath, QSettings::IniFormat);
    s.setValue(QStringLiteral("notice/notificationReadingAccepted"), true);
    s.sync();
}

} // namespace uibridge
