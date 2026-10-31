// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Settings as QML sees them. Owns the settings::Settings object: every setter writes the file at
// once, then emits written() so the caller can tell the service (ReloadSettings).
#ifndef SETTINGSBRIDGE_H
#define SETTINGSBRIDGE_H

#include "settings.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

namespace uibridge {

class SettingsBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool masterOn READ masterOn WRITE setMasterOn NOTIFY masterOnChanged)
    Q_PROPERTY(int intervalMinutes READ intervalMinutes WRITE setIntervalMinutes NOTIFY intervalChanged)
    // [{minutes, label}], the allowed list in order (a choice, not free input)
    Q_PROPERTY(QVariantList intervalChoices READ intervalChoices CONSTANT)
    Q_PROPERTY(int intervalIndex READ intervalIndex NOTIFY intervalChanged)
    Q_PROPERTY(bool debugLog READ debugLog WRITE setDebugLog NOTIFY debugLogChanged)
    Q_PROPERTY(QStringList blacklist READ blacklist NOTIFY blacklistChanged)
public:
    // `uiPrefsPath` holds the one UI-private flag (the first-switch-on notice).
    explicit SettingsBridge(const QString &settingsPath = settings::defaultSettingsPath(),
                            const QString &uiPrefsPath = defaultUiPrefsPath(),
                            QObject *parent = 0);

    static QString defaultUiPrefsPath();

    settings::Settings *settings() { return &m_settings; }

    bool masterOn() const { return m_settings.masterOn(); }
    bool setMasterOn(bool on);
    int intervalMinutes() const { return m_settings.intervalMinutes(); }
    bool setIntervalMinutes(int minutes);
    QVariantList intervalChoices() const;
    int intervalIndex() const;
    bool debugLog() const { return m_settings.debugLog(); }
    bool setDebugLog(bool on);
    QStringList blacklist() const { return m_settings.blacklist(); }

    // false for an empty or blank text, or one that is already listed
    // (compared without case). The text is stored trimmed.
    Q_INVOKABLE bool addBlacklist(const QString &text);
    Q_INVOKABLE bool removeBlacklist(int index);

    // The first time the master switch goes on the UI shows the notice of; the answer is
    // remembered.
    Q_INVOKABLE bool notificationNoticeNeeded() const;
    Q_INVOKABLE void acceptNotificationNotice();

public slots:
    // Re-reads the file (the service writes it too: discovered apps).
    void reload();

signals:
    void masterOnChanged();
    void intervalChanged();
    void debugLogChanged();
    void blacklistChanged();
    void written();     // a setter changed the file

private slots:
    void onSettingsChanged();

private:
    settings::Settings m_settings;
    QString m_prefsPath;
    bool m_master;
    int m_interval;
    bool m_debug;
    QStringList m_blacklist;
};

} // namespace uibridge

#endif // SETTINGSBRIDGE_H
