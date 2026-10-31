// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "silhouettecatalog.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

namespace uibridge {

SilhouetteCatalog::SilhouetteCatalog(const QString &dir, QObject *parent)
    : QAbstractListModel(parent)
{
    load(dir);
}

QString SilhouetteCatalog::installedDir()
{
    return QStringLiteral("/usr/share/sfos-casio-sync/images/silhouettes");
}

QString SilhouetteCatalog::fallbackId()
{
    return QStringLiteral("classic");
}

bool SilhouetteCatalog::load(const QString &dir)
{
    beginResetModel();
    m_dir = dir;
    m_entries.clear();
    bool ok = false;
    QFile f(dir + QStringLiteral("/silhouettes.json"));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        const QJsonArray arr = doc.object().value(QStringLiteral("silhouettes")).toArray();
        for (int i = 0; i < arr.size(); ++i) {
            const QJsonObject o = arr.at(i).toObject();
            SilhouetteInfo s;
            s.id = o.value(QStringLiteral("id")).toString();
            s.label = o.value(QStringLiteral("label")).toString();
            s.file = o.value(QStringLiteral("file")).toString();
            const QJsonArray vb = o.value(QStringLiteral("viewBox")).toArray();
            if (vb.size() == 4) {
                s.width = vb.at(2).toDouble();
                s.height = vb.at(3).toDouble();
            }
            // The file must stay inside the folder; the box must have a size.
            if (s.id.isEmpty() || s.file.isEmpty() || s.file.contains(QLatin1Char('/'))
                || s.file.contains(QStringLiteral("..")) || !(s.width > 0) || !(s.height > 0))
                continue;
            if (s.label.isEmpty())
                s.label = s.id;
            if (indexOf(s.id) >= 0)
                continue;
            m_entries.append(s);
        }
        ok = !m_entries.isEmpty();
    }
    endResetModel();
    emit countChanged();
    return ok;
}

int SilhouetteCatalog::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant SilhouetteCatalog::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return QVariant();
    const SilhouetteInfo &s = m_entries.at(index.row());
    switch (role) {
    case IdRole: return s.id;
    case LabelRole: return s.label;
    case SourceRole: return sourceOf(s.id);
    case AspectRole: return s.width / s.height;
    }
    return QVariant();
}

QHash<int, QByteArray> SilhouetteCatalog::roleNames() const
{
    QHash<int, QByteArray> r;
    r.insert(IdRole, "silhouetteId");
    r.insert(LabelRole, "label");
    r.insert(SourceRole, "source");
    r.insert(AspectRole, "aspect");
    return r;
}

int SilhouetteCatalog::indexOf(const QString &id) const
{
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries.at(i).id == id)
            return i;
    return -1;
}

bool SilhouetteCatalog::contains(const QString &id) const
{
    return indexOf(id) >= 0;
}

QString SilhouetteCatalog::effectiveId(const QString &silhouette, const QString &defaultId) const
{
    if (contains(silhouette))
        return silhouette;
    if (contains(defaultId))
        return defaultId;
    return fallbackId();
}

QString SilhouetteCatalog::labelOf(const QString &id) const
{
    const int i = indexOf(id);
    return i < 0 ? QString() : m_entries.at(i).label;
}

QString SilhouetteCatalog::sourceOf(const QString &id) const
{
    const int i = indexOf(id);
    if (i < 0)
        return QString();
    return QUrl::fromLocalFile(m_dir + QLatin1Char('/') + m_entries.at(i).file).toString();
}

double SilhouetteCatalog::aspectOf(const QString &id) const
{
    const int i = indexOf(id);
    return i < 0 ? 0.0 : m_entries.at(i).width / m_entries.at(i).height;
}

} // namespace uibridge
