// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "appsmodel.h"

#include <algorithm>

namespace uibridge {

AppsModel::AppsModel(settings::Settings *settings, QObject *parent)
    : QAbstractListModel(parent), m_settings(settings)
{
    connect(m_settings, SIGNAL(changed()), this, SLOT(rebuild()));
    rebuild();
}

static bool nameLess(const QString &a, const QString &b)
{
    const int c = a.compare(b, Qt::CaseInsensitive);
    return c != 0 ? c < 0 : a < b;
}

void AppsModel::rebuild()
{
    QStringList names = m_settings->apps();
    std::sort(names.begin(), names.end(), nameLess);
    QList<bool> enabled;
    for (int i = 0; i < names.size(); ++i)
        enabled.append(m_settings->isAppEnabled(names.at(i)));

    if (names == m_names) {
        // Same rows: only the switches may differ, so the view keeps its position.
        for (int i = 0; i < names.size(); ++i) {
            if (enabled.at(i) != m_enabled.at(i)) {
                m_enabled[i] = enabled.at(i);
                const QModelIndex ix = index(i);
                emit dataChanged(ix, ix, QVector<int>() << EnabledRole);
            }
        }
        return;
    }
    const int before = m_names.size();
    beginResetModel();
    m_names = names;
    m_enabled = enabled;
    endResetModel();
    if (before != m_names.size())
        emit countChanged();
}

int AppsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_names.size();
}

QVariant AppsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_names.size())
        return QVariant();
    if (role == NameRole)
        return m_names.at(index.row());
    if (role == EnabledRole)
        return m_enabled.at(index.row());
    return QVariant();
}

QHash<int, QByteArray> AppsModel::roleNames() const
{
    QHash<int, QByteArray> r;
    r.insert(NameRole, "name");
    r.insert(EnabledRole, "enabled");
    return r;
}

bool AppsModel::setEnabled(int row, bool enabled)
{
    if (row < 0 || row >= m_names.size())
        return false;
    m_settings->reload();
    if (!m_settings->setAppEnabled(m_names.at(row), enabled))
        return false;
    emit written();
    return true;
}

bool AppsModel::remove(int row)
{
    if (row < 0 || row >= m_names.size())
        return false;
    m_settings->reload();
    if (!m_settings->removeApp(m_names.at(row)))
        return false;
    emit written();
    return true;
}

} // namespace uibridge
