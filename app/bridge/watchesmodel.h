// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// The watches as the pages see them. Rows = the watches of the settings, in list order (= order of
// the sessions, R-GEN-5), then the paired devices that the last ListWatches answer found and that
// are not in the settings yet. Live fields come from the service's GetStatus; when the service is
// absent they come from state.json (read only) and the model table, so the pages keep working on
// settings.
#ifndef WATCHESMODEL_H
#define WATCHESMODEL_H

#include "modeltable.h"
#include "settings.h"
#include "silhouettecatalog.h"

#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QSortFilterProxyModel>
#include <QVariantMap>
#include <functional>

namespace uibridge {

struct WatchRow {
    QString key;
    QString alias;
    QString deviceName;
    QString modelId;
    QString modelLabel;
    QString supportStatus;      // supported | untested | unknown
    QString silhouetteStored;   // as in the settings, may be empty
    QString defaultSilhouette;
    QString silhouetteId;       // effective id
    int found;                  // 1 found now, 0 not found, -1 not known
    bool inSettings;
    bool enabled;
    bool riskAcknowledged;
    bool usable;
    QString problem;
    bool live;                  // the live fields come from the service
    bool running;
    qlonglong lastSuccess;      // seconds since the epoch, 0 = never
    qlonglong lastAttempt;
    qlonglong lastOpTime;
    qlonglong nextSync;
    QString lastError;
    QString lastOpKind;
    WatchRow() : found(-1), inSettings(false), enabled(false), riskAcknowledged(false),
                 usable(false), live(false), running(false), lastSuccess(0), lastAttempt(0),
                 lastOpTime(0), nextSync(0) {}
    QString title() const;      // alias, else the device name, else the key
};

// One watch for a detail page: stays valid and updates while the row exists.
class WatchItem : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
    Q_PROPERTY(QString key READ key CONSTANT)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString alias READ alias NOTIFY changed)
    Q_PROPERTY(QString deviceName READ deviceName NOTIFY changed)
    Q_PROPERTY(QString modelLabel READ modelLabel NOTIFY changed)
    Q_PROPERTY(QString supportStatus READ supportStatus NOTIFY changed)
    Q_PROPERTY(QString silhouetteId READ silhouetteId NOTIFY changed)
    Q_PROPERTY(int found READ found NOTIFY changed)
    Q_PROPERTY(bool inSettings READ inSettings NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool riskAcknowledged READ riskAcknowledged NOTIFY changed)
    Q_PROPERTY(bool usable READ usable NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(qlonglong lastSuccess READ lastSuccess NOTIFY changed)
    Q_PROPERTY(qlonglong lastOpTime READ lastOpTime NOTIFY changed)
    Q_PROPERTY(QString lastOpKind READ lastOpKind NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)
public:
    explicit WatchItem(const QString &key, QObject *parent = 0) : QObject(parent), m_valid(false)
    { m_row.key = key; }
    void setRow(const WatchRow &row, bool valid);
    bool valid() const { return m_valid; }
    QString key() const { return m_row.key; }
    QString title() const { return m_row.title(); }
    QString alias() const { return m_row.alias; }
    QString deviceName() const { return m_row.deviceName; }
    QString modelLabel() const { return m_row.modelLabel; }
    QString supportStatus() const { return m_row.supportStatus; }
    QString silhouetteId() const { return m_row.silhouetteId; }
    int found() const { return m_row.found; }
    bool inSettings() const { return m_row.inSettings; }
    bool enabled() const { return m_row.enabled; }
    bool riskAcknowledged() const { return m_row.riskAcknowledged; }
    bool usable() const { return m_row.usable; }
    bool running() const { return m_row.running; }
    qlonglong lastSuccess() const { return m_row.lastSuccess; }
    qlonglong lastOpTime() const { return m_row.lastOpTime; }
    QString lastOpKind() const { return m_row.lastOpKind; }
    QString lastError() const { return m_row.lastError; }
    QString problem() const { return m_row.problem; }
signals:
    void changed();
private:
    WatchRow m_row;
    bool m_valid;
};

class WatchesModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int enabledCount READ enabledCount NOTIFY countChanged)
public:
    enum Roles {
        KeyRole = Qt::UserRole + 1, TitleRole, AliasRole, DeviceNameRole, ModelIdRole,
        ModelLabelRole, SupportStatusRole, SilhouetteIdRole, FoundRole, InSettingsRole,
        EnabledRole, RiskAcknowledgedRole, UsableRole, ProblemRole, LiveRole, RunningRole,
        LastSuccessRole, LastAttemptRole, LastOpTimeRole, NextSyncRole, LastErrorRole,
        LastOpKindRole
    };

    // All pointers stay owned by the caller and must outlive the model.
    WatchesModel(settings::Settings *settings, const watchproto::ModelTable *models,
                 const SilhouetteCatalog *silhouettes,
                 const QString &statePath = settings::defaultStatePath(), QObject *parent = 0);

    int count() const { return m_rows.size(); }
    int enabledCount() const;
    int rowCount(const QModelIndex &parent = QModelIndex()) const;
    QVariant data(const QModelIndex &index, int role) const;
    QHash<int, QByteArray> roleNames() const;
    const QList<WatchRow> &rows() const { return m_rows; }

    // From the service: the `watches` of GetStatus (empty when `serviceUp` is
    // false), and the answer of ListWatches.
    void setServiceStatus(const QList<QVariantMap> &watches, bool serviceUp);
    void setListed(const QList<QVariantMap> &listed);

    // Oldest last success among the usable enabled watches, 0 if one never synced; anyUsable()
    // false if there is none (cover).
    qlonglong oldestLastSuccess() const;
    bool anyUsable() const;

    Q_INVOKABLE QObject *item(const QString &key);   // a WatchItem (QML needs a QObject* here)
    WatchItem *watchItem(const QString &key);
    Q_INVOKABLE int indexOfKey(const QString &key) const;

    // ---- edits; each writes the settings file and returns false if nothing was written
    Q_INVOKABLE bool setEnabled(const QString &key, bool on);
    // The "Enable at my risk" answer: acknowledges and enables.
    Q_INVOKABLE bool enableAtOwnRisk(const QString &key);
    Q_INVOKABLE bool setAlias(const QString &key, const QString &alias);
    // Only an id of the catalog is accepted.
    Q_INVOKABLE bool setSilhouette(const QString &key, const QString &id);
    Q_INVOKABLE bool forget(const QString &key);
    // Order of the sessions; only rows that are in the settings move.
    Q_INVOKABLE bool canMoveUp(int row) const;
    Q_INVOKABLE bool canMoveDown(int row) const;
    Q_INVOKABLE bool moveUp(int row);
    Q_INVOKABLE bool moveDown(int row);

public slots:
    void rebuild();

signals:
    void countChanged();
    void rebuilt();
    void written();     // the settings file was changed by this model

private:
    const watchproto::ModelInfo *modelOf(const QString &modelId, const QString &deviceName) const;
    void fillModelFacts(WatchRow *r) const;
    QList<WatchRow> buildRows() const;
    bool writeEntry(const QString &key, bool create, const std::function<void(settings::WatchEntry *)> &edit);

    settings::Settings *m_settings;
    const watchproto::ModelTable *m_models;
    const SilhouetteCatalog *m_silhouettes;
    QString m_statePath;
    QList<QVariantMap> m_statusWatches;
    bool m_serviceUp;
    QList<QVariantMap> m_listed;
    QList<WatchRow> m_rows;
    QHash<QString, WatchItem *> m_items;
};

// The Status page shows one block per enabled watch, in list order.
class EnabledWatchesModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit EnabledWatchesModel(WatchesModel *source, QObject *parent = 0);
    int count() const { return rowCount(); }
signals:
    void countChanged();
protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const;
private slots:
    void emitCount() { emit countChanged(); }
};

} // namespace uibridge

#endif // WATCHESMODEL_H
