// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// Model table (R-GEN-3): which watch names are known and their per-model parameters, loaded from
// models.json.
#ifndef MODELTABLE_H
#define MODELTABLE_H

#include "watchproto.h"

#include <QByteArray>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

namespace watchproto {

enum ModelStatus {
    ModelSupported, // tested on hardware
    ModelUntested   // shown as such in the UI
};

struct ModelInfo {
    QString id;
    QString label;
    QString family;
    ModelStatus status;
    DowEncoding dowEncoding;
    QString defaultSilhouette;      // optional; empty = not given
    QStringList patterns;           // source text, as in the file
    QList<QRegularExpression> regexes;
    ModelInfo() : status(ModelUntested), dowEncoding(DowSun0) {}
};

class ModelTable
{
public:
    // Parses models.json content. On failure returns false, leaves the table
    // empty and describes the problem in *error (if given).
    bool load(const QByteArray &json, QString *error = 0);

    // First entry (in file order) with a pattern matching the whole device
    // name, or 0. The pointer stays valid until the next load().
    const ModelInfo *find(const QString &deviceName) const;

    int count() const { return m_models.size(); }
    const QList<ModelInfo> &models() const { return m_models; }

private:
    QList<ModelInfo> m_models;
};

} // namespace watchproto

#endif // MODELTABLE_H
