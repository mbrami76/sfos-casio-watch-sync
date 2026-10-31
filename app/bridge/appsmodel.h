// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// The apps seen so far, sorted by name, with their on/off switch. Reads and writes through
// settings::Settings; blacklisted apps are not listed (Settings::apps() already leaves them out).
#ifndef APPSMODEL_H
#define APPSMODEL_H

#include "settings.h"

#include <QAbstractListModel>
#include <QStringList>

namespace uibridge {

class AppsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles { NameRole = Qt::UserRole + 1, EnabledRole };

    explicit AppsModel(settings::Settings *settings, QObject *parent = 0);

    int count() const { return m_names.size(); }
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;
    QHash<int, QByteArray> roleNames() const;

    Q_INVOKABLE bool setEnabled(int row, bool enabled);
    // "Remove from list": the app returns, disabled, with its next notification.
    Q_INVOKABLE bool remove(int row);

public slots:
    void rebuild();

signals:
    void countChanged();
    void written();     // the settings file was changed by this model

private:
    settings::Settings *m_settings;
    QStringList m_names;
    QList<bool> m_enabled;
};

} // namespace uibridge

#endif // APPSMODEL_H
