// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// The five watch silhouettes. The index silhouettes.json is the single source of ids and labels;
// nothing is listed twice.
#ifndef SILHOUETTECATALOG_H
#define SILHOUETTECATALOG_H

#include <QAbstractListModel>
#include <QList>
#include <QString>

namespace uibridge {

struct SilhouetteInfo {
    QString id;
    QString label;
    QString file;       // file name inside the folder
    double width;       // viewBox, tight (equal to the drawing)
    double height;
    SilhouetteInfo() : width(0), height(0) {}
};

class SilhouetteCatalog : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles { IdRole = Qt::UserRole + 1, LabelRole, SourceRole, AspectRole };

    // Folder with silhouettes.json and the SVG files; loaded at once.
    explicit SilhouetteCatalog(const QString &dir = installedDir(), QObject *parent = 0);

    static QString installedDir();          // /usr/share/sfos-casio-sync/images/silhouettes
    static QString fallbackId();            // "classic"

    // Entries with an empty id, an unsafe file name or a bad viewBox are skipped.
    bool load(const QString &dir);
    QList<SilhouetteInfo> entries() const { return m_entries; }

    int count() const { return m_entries.size(); }
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;
    QHash<int, QByteArray> roleNames() const;

    Q_INVOKABLE bool contains(const QString &id) const;
    // The rule of the `silhouette` if it is a known id, else `defaultId` if known, else "classic".
    Q_INVOKABLE QString effectiveId(const QString &silhouette, const QString &defaultId) const;
    Q_INVOKABLE QString labelOf(const QString &id) const;
    // file:// URL of the SVG, empty if unknown.
    Q_INVOKABLE QString sourceOf(const QString &id) const;
    // viewBox width / height, 0 if unknown.
    Q_INVOKABLE double aspectOf(const QString &id) const;

signals:
    void countChanged();

private:
    int indexOf(const QString &id) const;
    QString m_dir;
    QList<SilhouetteInfo> m_entries;
};

} // namespace uibridge

#endif // SILHOUETTECATALOG_H
