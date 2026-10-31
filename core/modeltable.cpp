// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "modeltable.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace watchproto {

namespace {

bool fail(QString *error, const QString &msg)
{
    if (error) *error = msg;
    return false;
}

bool requiredString(const QJsonObject &o, const char *key, const QString &where,
                    QString *out, QString *error)
{
    const QJsonValue v = o.value(QLatin1String(key));
    if (!v.isString() || v.toString().isEmpty())
        return fail(error, where + QLatin1String(": '") + QLatin1String(key)
                    + QLatin1String("' must be a non-empty string"));
    *out = v.toString();
    return true;
}

} // namespace

bool ModelTable::load(const QByteArray &json, QString *error)
{
    m_models.clear();

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    if (pe.error != QJsonParseError::NoError)
        return fail(error, QLatin1String("models.json: ") + pe.errorString());
    if (!doc.isObject())
        return fail(error, QLatin1String("models.json: top level must be an object"));
    const QJsonObject root = doc.object();
    if (root.value(QLatin1String("version")).toInt(-1) != 1)
        return fail(error, QLatin1String("models.json: 'version' must be 1"));
    if (!root.value(QLatin1String("models")).isArray())
        return fail(error, QLatin1String("models.json: 'models' must be an array"));

    QList<ModelInfo> parsed;
    QSet<QString> ids;
    const QJsonArray arr = root.value(QLatin1String("models")).toArray();
    for (int i = 0; i < arr.size(); ++i) {
        const QString where = QLatin1String("models[") + QString::number(i) + QLatin1String("]");
        if (!arr.at(i).isObject())
            return fail(error, where + QLatin1String(": must be an object"));
        const QJsonObject o = arr.at(i).toObject();

        ModelInfo m;
        QString status;
        if (!requiredString(o, "id", where, &m.id, error)
            || !requiredString(o, "label", where, &m.label, error)
            || !requiredString(o, "family", where, &m.family, error)
            || !requiredString(o, "status", where, &status, error))
            return false;
        if (ids.contains(m.id))
            return fail(error, where + QLatin1String(": duplicate id '") + m.id + QLatin1String("'"));
        ids.insert(m.id);

        if (status == QLatin1String("supported")) m.status = ModelSupported;
        else if (status == QLatin1String("untested")) m.status = ModelUntested;
        else return fail(error, where + QLatin1String(": 'status' must be 'supported' or 'untested'"));

        const QJsonValue pv = o.value(QLatin1String("namePatterns"));
        if (!pv.isArray() || pv.toArray().isEmpty())
            return fail(error, where + QLatin1String(": 'namePatterns' must be a non-empty array"));
        const QJsonArray pats = pv.toArray();
        for (int k = 0; k < pats.size(); ++k) {
            if (!pats.at(k).isString())
                return fail(error, where + QLatin1String(": patterns must be strings"));
            const QString p = pats.at(k).toString();
            // The whole device name must match.
            QRegularExpression re(QLatin1String("\\A(?:") + p + QLatin1String(")\\z"));
            if (p.isEmpty() || !re.isValid())
                return fail(error, where + QLatin1String(": invalid pattern '") + p + QLatin1String("'"));
            m.patterns << p;
            m.regexes << re;
        }

        // Optional; a value that is not a string counts as absent.
        m.defaultSilhouette = o.value(QLatin1String("defaultSilhouette")).toString();

        if (o.contains(QLatin1String("params"))) {
            if (!o.value(QLatin1String("params")).isObject())
                return fail(error, where + QLatin1String(": 'params' must be an object"));
            const QJsonValue dv = o.value(QLatin1String("params")).toObject()
                                      .value(QLatin1String("dowEncoding"));
            if (!dv.isUndefined()) {
                if (dv.toString() == QLatin1String("sun0")) m.dowEncoding = DowSun0;
                else if (dv.toString() == QLatin1String("mon1")) m.dowEncoding = DowMon1;
                else return fail(error, where + QLatin1String(": 'dowEncoding' must be 'sun0' or 'mon1'"));
            }
        }
        parsed << m;
    }
    m_models = parsed;
    return true;
}

const ModelInfo *ModelTable::find(const QString &deviceName) const
{
    for (int i = 0; i < m_models.size(); ++i) {
        const QList<QRegularExpression> &res = m_models.at(i).regexes;
        for (int k = 0; k < res.size(); ++k) {
            if (res.at(k).match(deviceName).hasMatch())
                return &m_models.at(i);
        }
    }
    return 0;
}

} // namespace watchproto
