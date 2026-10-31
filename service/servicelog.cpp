// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "servicelog.h"

#include <QAtomicInt>
#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

#include <stdio.h>

namespace service {

namespace {
QAtomicInt g_on(0);
QMutex g_mutex;
QtMessageHandler g_previous = 0;

// The service is started by the session bus, so its stderr goes nowhere a user can read.
// The log is therefore also appended to a file in the cache folder. When it grows past the
// limit it is moved to debug.log.1 (replacing the previous one), which bounds the disk use.
const qint64 kMaxLogBytes = 512 * 1024;

QString logFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
           + QStringLiteral("/it.michelebrami/sfos-casio-sync/debug.log");
}

void appendToFile(const QByteArray &line)
{
    QMutexLocker lock(&g_mutex);
    const QString path = logFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (f.size() > kMaxLogBytes) {
        QFile::remove(path + QStringLiteral(".1"));
        f.rename(path + QStringLiteral(".1"));
    }
    QFile out(path);
    if (out.open(QIODevice::Append)) {
        out.write(line);
        out.close();
    }
}

QByteArray stamped(const QString &text)
{
    return (QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))
            + QLatin1String("  ") + text + QLatin1Char('\n')).toLocal8Bit();
}

// The notification monitor reports through qDebug(); while the debug log is on those lines
// are also written to the file. Every message still goes to the handler that was installed
// before, so warnings are not lost when the debug log is off.
void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (g_on.load())
        appendToFile(stamped(msg));
    if (g_previous)
        g_previous(type, ctx, msg);
}
} // namespace

void setDebugLog(bool on)
{
    g_on.store(on ? 1 : 0);
    static bool installed = false;
    if (on && !installed) {
        installed = true;
        g_previous = qInstallMessageHandler(messageHandler);
    }
}

bool debugLogOn()
{
    return g_on.load() != 0;
}

void debugLog(const QString &line)
{
    if (!debugLogOn())
        return;
    const QByteArray s = stamped(line);
    fputs(s.constData(), stderr);
    fflush(stderr);
    appendToFile(s);
}

} // namespace service
