// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "watchesmodel.h"

namespace uibridge {

QString WatchRow::title() const
{
    if (!alias.trimmed().isEmpty())
        return alias;
    if (!deviceName.isEmpty())
        return deviceName;
    return key;
}

void WatchItem::setRow(const WatchRow &row, bool valid)
{
    m_row = row;
    m_valid = valid;
    emit changed();
}

static qlonglong secs(const QDateTime &t)
{
    return t.isValid() ? t.toMSecsSinceEpoch() / 1000 : 0;
}

WatchesModel::WatchesModel(settings::Settings *settings, const watchproto::ModelTable *models,
                           const SilhouetteCatalog *silhouettes, const QString &statePath,
                           QObject *parent)
    : QAbstractListModel(parent), m_settings(settings), m_models(models),
      m_silhouettes(silhouettes), m_statePath(statePath), m_serviceUp(false)
{
    connect(m_settings, SIGNAL(changed()), this, SLOT(rebuild()));
    m_rows = buildRows();
}

int WatchesModel::enabledCount() const
{
    int n = 0;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).enabled)
            ++n;
    return n;
}

int WatchesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

QHash<int, QByteArray> WatchesModel::roleNames() const
{
    QHash<int, QByteArray> r;
    r.insert(KeyRole, "key");
    r.insert(TitleRole, "title");
    r.insert(AliasRole, "alias");
    r.insert(DeviceNameRole, "deviceName");
    r.insert(ModelIdRole, "modelId");
    r.insert(ModelLabelRole, "modelLabel");
    r.insert(SupportStatusRole, "supportStatus");
    r.insert(SilhouetteIdRole, "silhouetteId");
    r.insert(FoundRole, "found");
    r.insert(InSettingsRole, "inSettings");
    r.insert(EnabledRole, "enabled");
    r.insert(RiskAcknowledgedRole, "riskAcknowledged");
    r.insert(UsableRole, "usable");
    r.insert(ProblemRole, "problem");
    r.insert(LiveRole, "live");
    r.insert(RunningRole, "running");
    r.insert(LastSuccessRole, "lastSuccess");
    r.insert(LastAttemptRole, "lastAttempt");
    r.insert(LastOpTimeRole, "lastOpTime");
    r.insert(NextSyncRole, "nextSync");
    r.insert(LastErrorRole, "lastError");
    r.insert(LastOpKindRole, "lastOpKind");
    return r;
}

QVariant WatchesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size())
        return QVariant();
    const WatchRow &r = m_rows.at(index.row());
    switch (role) {
    case KeyRole: return r.key;
    case TitleRole: return r.title();
    case AliasRole: return r.alias;
    case DeviceNameRole: return r.deviceName;
    case ModelIdRole: return r.modelId;
    case ModelLabelRole: return r.modelLabel;
    case SupportStatusRole: return r.supportStatus;
    case SilhouetteIdRole: return r.silhouetteId;
    case FoundRole: return r.found;
    case InSettingsRole: return r.inSettings;
    case EnabledRole: return r.enabled;
    case RiskAcknowledgedRole: return r.riskAcknowledged;
    case UsableRole: return r.usable;
    case ProblemRole: return r.problem;
    case LiveRole: return r.live;
    case RunningRole: return r.running;
    case LastSuccessRole: return r.lastSuccess;
    case LastAttemptRole: return r.lastAttempt;
    case LastOpTimeRole: return r.lastOpTime;
    case NextSyncRole: return r.nextSync;
    case LastErrorRole: return r.lastError;
    case LastOpKindRole: return r.lastOpKind;
    }
    return QVariant();
}

const watchproto::ModelInfo *WatchesModel::modelOf(const QString &modelId,
                                                   const QString &deviceName) const
{
    if (!m_models)
        return 0;
    if (!modelId.isEmpty()) {
        const QList<watchproto::ModelInfo> &all = m_models->models();
        for (int i = 0; i < all.size(); ++i)
            if (all.at(i).id == modelId)
                return &all.at(i);
    }
    return deviceName.isEmpty() ? 0 : m_models->find(deviceName);
}

// Label, support status and default silhouette from the local model table
// (the service's values replace them when it answers).
void WatchesModel::fillModelFacts(WatchRow *r) const
{
    const watchproto::ModelInfo *m = modelOf(r->modelId, r->deviceName);
    if (m) {
        r->modelLabel = m->label;
        r->supportStatus = m->status == watchproto::ModelSupported ? QStringLiteral("supported")
                                                                   : QStringLiteral("untested");
        r->defaultSilhouette = m->defaultSilhouette;
    } else {
        r->modelLabel = r->deviceName;
        r->supportStatus = QStringLiteral("unknown");
    }
}

static QVariantMap findByKey(const QList<QVariantMap> &list, const QString &key)
{
    for (int i = 0; i < list.size(); ++i)
        if (list.at(i).value(QStringLiteral("key")).toString() == key)
            return list.at(i);
    return QVariantMap();
}

QList<WatchRow> WatchesModel::buildRows() const
{
    QList<WatchRow> rows;
    settings::State state(m_statePath);
    if (!m_serviceUp)
        state.load();
    QStringList seen;

    const QList<settings::WatchEntry> entries = m_settings->watches();
    for (int i = 0; i < entries.size(); ++i) {
        const settings::WatchEntry &e = entries.at(i);
        WatchRow r;
        r.key = e.key;
        r.alias = e.alias;
        r.deviceName = e.deviceName;
        r.modelId = e.modelId;
        r.silhouetteStored = e.silhouette;
        r.inSettings = true;
        r.enabled = e.enabled;
        r.riskAcknowledged = e.riskAcknowledged;
        fillModelFacts(&r);

        const QVariantMap st = findByKey(m_statusWatches, e.key);
        if (!st.isEmpty()) {
            r.live = true;
            if (st.contains(QStringLiteral("modelLabel")) && !st.value("modelLabel").toString().isEmpty())
                r.modelLabel = st.value("modelLabel").toString();
            if (!st.value("supportStatus").toString().isEmpty())
                r.supportStatus = st.value("supportStatus").toString();
            if (!st.value("defaultSilhouette").toString().isEmpty())
                r.defaultSilhouette = st.value("defaultSilhouette").toString();
            r.usable = st.value("usable").toBool();
            r.problem = st.value("problem").toString();
            r.running = st.value("running").toBool();
            r.lastSuccess = st.value("lastSuccess").toLongLong();
            r.lastAttempt = st.value("lastAttempt").toLongLong();
            r.lastOpTime = st.value("lastOpTime").toLongLong();
            r.nextSync = st.value("nextSync").toLongLong();
            r.lastError = st.value("lastError").toString();
            r.lastOpKind = st.value("lastOpKind").toString();
        } else {
            // No service: what the state file says; usable by the same rule as the service.
            const settings::WatchState ws = state.watch(e.key);
            r.lastSuccess = secs(ws.lastSuccess);
            r.lastAttempt = secs(ws.lastAttempt);
            r.lastOpTime = secs(ws.lastOpTime);
            r.lastError = ws.lastError;
            r.lastOpKind = ws.lastOpKind;
            r.usable = e.enabled && r.supportStatus != QLatin1String("unknown")
                       && (r.supportStatus == QLatin1String("supported") || e.riskAcknowledged);
        }
        const QVariantMap li = findByKey(m_listed, e.key);
        if (!li.isEmpty())
            r.found = li.value("foundNow").toBool() ? 1 : 0;
        r.silhouetteId = m_silhouettes->effectiveId(r.silhouetteStored, r.defaultSilhouette);
        rows.append(r);
        seen.append(e.key);
    }

    // Paired devices of the model table that are not in the settings yet (and not forgotten).
    const QStringList hidden = m_settings->hiddenDevices();
    for (int i = 0; i < m_listed.size(); ++i) {
        const QVariantMap &li = m_listed.at(i);
        const QString key = li.value("key").toString();
        if (key.isEmpty() || seen.contains(key) || hidden.contains(key))
            continue;
        WatchRow r;
        r.key = key;
        r.deviceName = li.value("deviceName").toString();
        if (r.deviceName.isEmpty())
            r.deviceName = li.value("bluezAlias").toString();
        r.modelId = li.value("modelId").toString();
        fillModelFacts(&r);
        if (!li.value("modelLabel").toString().isEmpty())
            r.modelLabel = li.value("modelLabel").toString();
        if (!li.value("supportStatus").toString().isEmpty())
            r.supportStatus = li.value("supportStatus").toString();
        if (!li.value("defaultSilhouette").toString().isEmpty())
            r.defaultSilhouette = li.value("defaultSilhouette").toString();
        r.found = li.value("foundNow").toBool() ? 1 : 0;
        r.silhouetteId = m_silhouettes->effectiveId(QString(), r.defaultSilhouette);
        rows.append(r);
        seen.append(key);
    }
    return rows;
}

void WatchesModel::rebuild()
{
    const QList<WatchRow> rows = buildRows();
    bool sameKeys = rows.size() == m_rows.size();
    for (int i = 0; sameKeys && i < rows.size(); ++i)
        sameKeys = rows.at(i).key == m_rows.at(i).key;

    const int before = m_rows.size();
    const int enabledBefore = enabledCount();
    if (sameKeys) {
        // Same rows in the same order: update in place so the view keeps its position.
        m_rows = rows;
        if (!rows.isEmpty())
            emit dataChanged(index(0), index(rows.size() - 1));
    } else {
        beginResetModel();
        m_rows = rows;
        endResetModel();
    }
    for (QHash<QString, WatchItem *>::const_iterator it = m_items.constBegin();
         it != m_items.constEnd(); ++it) {
        const int i = indexOfKey(it.key());
        if (i >= 0)
            it.value()->setRow(m_rows.at(i), true);
        else
            it.value()->setRow(WatchRow(), false);
    }
    if (before != m_rows.size() || enabledBefore != enabledCount())
        emit countChanged();
    emit rebuilt();
}

void WatchesModel::setServiceStatus(const QList<QVariantMap> &watches, bool serviceUp)
{
    m_statusWatches = watches;
    m_serviceUp = serviceUp;
    rebuild();
}

void WatchesModel::setListed(const QList<QVariantMap> &listed)
{
    m_listed = listed;
    rebuild();
}

qlonglong WatchesModel::oldestLastSuccess() const
{
    qlonglong oldest = -1;
    for (int i = 0; i < m_rows.size(); ++i) {
        const WatchRow &r = m_rows.at(i);
        if (!r.enabled || !r.usable)
            continue;
        if (oldest < 0 || r.lastSuccess < oldest)
            oldest = r.lastSuccess;
    }
    return oldest < 0 ? 0 : oldest;
}

bool WatchesModel::anyUsable() const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).enabled && m_rows.at(i).usable)
            return true;
    return false;
}

QObject *WatchesModel::item(const QString &key)
{
    return watchItem(key);
}

WatchItem *WatchesModel::watchItem(const QString &key)
{
    WatchItem *w = m_items.value(key);
    if (!w) {
        w = new WatchItem(key, this);
        m_items.insert(key, w);
        const int i = indexOfKey(key);
        if (i >= 0)
            w->setRow(m_rows.at(i), true);
    }
    return w;
}

int WatchesModel::indexOfKey(const QString &key) const
{
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).key == key)
            return i;
    return -1;
}

// ------------------------------------------------------------------- edits

// Applies `edit` to the entry with `key`. A device that is listed but not in
// the settings gets an entry (disabled, no alias) first when `create` is set.
bool WatchesModel::writeEntry(const QString &key, bool create,
                              const std::function<void(settings::WatchEntry *)> &edit)
{
    m_settings->reload();
    QList<settings::WatchEntry> list = m_settings->watches();
    int at = -1;
    for (int i = 0; i < list.size(); ++i)
        if (list.at(i).key == key)
            at = i;
    if (at < 0) {
        const int row = indexOfKey(key);
        if (!create || row < 0)
            return false;
        const WatchRow &r = m_rows.at(row);
        settings::WatchEntry e;
        e.key = r.key;
        e.address = r.key;      // the key is the address of the paired device (R-TIME-10)
        e.deviceName = r.deviceName;
        e.modelId = r.modelId;
        list.append(e);
        at = list.size() - 1;
        QStringList hidden = m_settings->hiddenDevices();
        if (hidden.removeAll(key) > 0)
            m_settings->setHiddenDevices(hidden);
    }
    edit(&list[at]);
    if (!m_settings->setWatches(list))
        return false;
    emit written();
    return true;
}

bool WatchesModel::setEnabled(const QString &key, bool on)
{
    return writeEntry(key, on, [on](settings::WatchEntry *e) { e->enabled = on; });
}

bool WatchesModel::enableAtOwnRisk(const QString &key)
{
    return writeEntry(key, true, [](settings::WatchEntry *e) {
        e->riskAcknowledged = true;
        e->enabled = true;
    });
}

bool WatchesModel::setAlias(const QString &key, const QString &alias)
{
    const QString a = alias.trimmed();
    return writeEntry(key, !a.isEmpty(), [a](settings::WatchEntry *e) { e->alias = a; });
}

bool WatchesModel::setSilhouette(const QString &key, const QString &id)
{
    if (!m_silhouettes->contains(id))
        return false;
    return writeEntry(key, true, [id](settings::WatchEntry *e) { e->silhouette = id; });
}

// Forget must work on every row the list shows. A row comes from one of two places: the settings
// file, or the paired devices that Bluetooth lists and that are not in the settings yet.
// Some watches (the DW-H5600 among them) appear under a new Bluetooth address after each
// pairing, so old settings rows go stale while the new address shows up as a listed-only row.
// Removing only settings entries left those listed-only rows impossible to forget (and the
// device would be offered again at every listing). So a listed device is also recorded as
// hidden; it is offered again only when the user adds it explicitly.
// The Bluetooth pairing itself is not touched.
bool WatchesModel::forget(const QString &key)
{
    m_settings->reload();
    QList<settings::WatchEntry> list = m_settings->watches();
    bool known = false;
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).key == key) {
            list.removeAt(i);
            if (!m_settings->setWatches(list))
                return false;
            known = true;
            break;
        }
    }
    // A paired device that is only listed would come back at the next listing: remember it.
    if (!findByKey(m_listed, key).isEmpty()) {
        QStringList hidden = m_settings->hiddenDevices();
        if (!hidden.contains(key)) {
            hidden.append(key);
            if (!m_settings->setHiddenDevices(hidden))
                return false;
            known = true;
        }
    }
    if (!known)
        return false;
    emit written();
    return true;
}

bool WatchesModel::canMoveUp(int row) const
{
    return row > 0 && row < m_rows.size() && m_rows.at(row).inSettings;
}

bool WatchesModel::canMoveDown(int row) const
{
    return row >= 0 && row + 1 < m_rows.size() && m_rows.at(row).inSettings
           && m_rows.at(row + 1).inSettings;
}

bool WatchesModel::moveUp(int row)
{
    if (!canMoveUp(row))
        return false;
    m_settings->reload();
    if (!m_settings->moveWatch(row, row - 1))
        return false;
    emit written();
    return true;
}

bool WatchesModel::moveDown(int row)
{
    if (!canMoveDown(row))
        return false;
    m_settings->reload();
    if (!m_settings->moveWatch(row, row + 1))
        return false;
    emit written();
    return true;
}

// ---------------------------------------------------------- enabled filter

EnabledWatchesModel::EnabledWatchesModel(WatchesModel *source, QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setSourceModel(source);
    connect(this, SIGNAL(rowsInserted(QModelIndex, int, int)), this, SLOT(emitCount()));
    connect(this, SIGNAL(rowsRemoved(QModelIndex, int, int)), this, SLOT(emitCount()));
    connect(this, SIGNAL(modelReset()), this, SLOT(emitCount()));
    connect(this, SIGNAL(layoutChanged()), this, SLOT(emitCount()));
    // A change of the switches re-evaluates the filter (dataChanged does it, dynamicSortFilter).
    setDynamicSortFilter(true);
    setFilterRole(WatchesModel::EnabledRole);
}

bool EnabledWatchesModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex ix = sourceModel()->index(sourceRow, 0, sourceParent);
    return sourceModel()->data(ix, WatchesModel::EnabledRole).toBool();
}

} // namespace uibridge
