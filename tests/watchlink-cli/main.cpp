// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// watchlink-cli: on-phone test program for watchlink. Runs the real BlueZ adapter on the system bus
// and prints a timestamped log of each step of a session. Nothing is written to disk and nothing
// leaves the phone. The log carries metadata only; the only text it shows is the one the test
// itself sends. Watch addresses appear only in the output of `list`.
//
// watchlink-cli [--models FILE] [--gap MS] list watchlink-cli [--models FILE] sync <watch>
// watchlink-cli [--models FILE] notify <watch> "text" watchlink-cli [--models FILE] burst <watch> N
// (N alerts, 1..20, one session)
//
// <watch> is the number shown by `list`, the Bluetooth address, or the Bluetooth alias
// (case-insensitive). --gap MS sets the pause between notification writes (default 600). Without
// --models a built-in table with the DW-H5600 is used.
#include "bluezadapter.h"
#include "scheduler.h"
#include "settings.h"
#include "watchlink.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QStringList>
#include <QTextStream>

#include <stdio.h>

using namespace watchlink;

namespace {

const char kBuiltinModels[] =
    "{\"version\":1,\"models\":[{\"id\":\"dw-h5600\",\"label\":\"Casio DW-H5600\","
    "\"family\":\"h5600\",\"status\":\"supported\",\"namePatterns\":[\"CASIO DW-H5600\"],"
    "\"params\":{\"dowEncoding\":\"sun0\"}}]}";

QTextStream &out()
{
    static QTextStream s(stdout);
    return s;
}

class StdoutLog : public LogSink
{
public:
    void log(const QString &line)
    {
        out() << QDateTime::currentDateTime().toString("HH:mm:ss.zzz") << "  " << line << endl;
    }
};

int usage()
{
    fprintf(stderr,
            "Usage: watchlink-cli [--models FILE] list\n"
            "       watchlink-cli [--models FILE] sync   <watch>\n"
            "       watchlink-cli [--models FILE] notify <watch> \"text\"\n"
            "       watchlink-cli [--models FILE] burst  <watch> N\n"
            "<watch>: number from `list`, Bluetooth address or alias.\n");
    return 2;
}

settings::WatchEntry entryOf(const FoundWatch &fw)
{
    settings::WatchEntry e;
    e.key = fw.device.address;
    e.address = fw.device.address;
    e.deviceName = fw.device.name;
    e.modelId = fw.model->id;
    e.alias = fw.device.alias;
    e.enabled = true;
    e.riskAcknowledged = true;      // a test tool: the user chose the watch
    return e;
}

void printResult(const SessionResult &r)
{
    out() << "result: " << (r.ok() ? "OK" : "FAILED")
          << ", connected=" << (r.connected ? "yes" : "no")
          << ", timeSent=" << (r.timeSent ? "yes" : "no")
          << ", alertsSent=" << r.alertsSent
          << ", alertsDropped=" << r.alertsDropped
          << ", alertsKept=" << r.alertsKept
          << ", watch address read=" << (r.bleAddress.isEmpty() ? "no" : "yes");
    if (!r.error.isEmpty())
        out() << ", error: " << r.error;
    out() << endl;
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments().mid(1);

    QString modelsFile;
    int mi = args.indexOf("--models");
    if (mi >= 0) {
        if (mi + 1 >= args.size())
            return usage();
        modelsFile = args.at(mi + 1);
        args.removeAt(mi + 1);
        args.removeAt(mi);
    }
    int gapMs = kWriteGapMs;
    int gi = args.indexOf("--gap");
    if (gi >= 0) {
        bool ok = false;
        if (gi + 1 >= args.size())
            return usage();
        gapMs = args.at(gi + 1).toInt(&ok);
        if (!ok || gapMs < 0)
            return usage();
        args.removeAt(gi + 1);
        args.removeAt(gi);
    }
    if (args.isEmpty())
        return usage();
    const QString cmd = args.at(0);

    QByteArray json(kBuiltinModels);
    if (!modelsFile.isEmpty()) {
        QFile f(modelsFile);
        if (!f.open(QIODevice::ReadOnly)) {
            fprintf(stderr, "Cannot read %s\n", qPrintable(modelsFile));
            return 2;
        }
        json = f.readAll();
    }
    watchproto::ModelTable models;
    QString err;
    if (!models.load(json, &err)) {
        fprintf(stderr, "Model table: %s\n", qPrintable(err));
        return 2;
    }

    scheduler::SystemClock clock;
    BluezDBusAdapter adapter;
    StdoutLog sink;
    WatchLink link(&adapter, &models, &clock);
    link.setLogSink(&sink);
    link.setWriteGapMs(gapMs);

    const QList<FoundWatch> found = link.findWatches(&err);
    if (!err.isEmpty()) {
        fprintf(stderr, "%s\n", qPrintable(err));
        return 1;
    }

    if (cmd == "list") {
        if (found.isEmpty())
            out() << "no paired watch matches the model table" << endl;
        for (int i = 0; i < found.size(); ++i) {
            const FoundWatch &w = found.at(i);
            out() << (i + 1) << "  " << w.device.address << "  " << w.device.name << "  alias \""
                  << w.device.alias << "\"  model " << w.model->id
                  << (w.model->status == watchproto::ModelSupported ? " (supported)" : " (untested)")
                  << "  " << (w.device.connected ? "connected" : "not connected") << endl;
        }
        return 0;
    }

    if ((cmd != "sync" && cmd != "notify" && cmd != "burst") || args.size() < 2)
        return usage();
    if ((cmd == "notify" && args.size() != 3) || (cmd == "burst" && args.size() != 3)
        || (cmd == "sync" && args.size() != 2))
        return usage();

    // Select the watch: number, address or alias.
    const QString sel = args.at(1);
    int idx = -1;
    bool isNumber = false;
    const int n = sel.toInt(&isNumber);
    if (isNumber && n >= 1 && n <= found.size())
        idx = n - 1;
    for (int i = 0; idx < 0 && i < found.size(); ++i) {
        if (found.at(i).device.address.compare(sel, Qt::CaseInsensitive) == 0
            || found.at(i).device.alias.compare(sel, Qt::CaseInsensitive) == 0)
            idx = i;
    }
    if (idx < 0) {
        fprintf(stderr, "No such watch: %s (see `list`)\n", qPrintable(sel));
        return 1;
    }
    const settings::WatchEntry entry = entryOf(found.at(idx));

    SessionRequest req;
    req.watch = entry;
    req.timeSync = (cmd == "sync");
    req.readBleAddress = true;

    if (cmd == "notify") {
        notiffilter::WatchNotification a;
        a.category = watchproto::kCategoryGeneric;
        a.sender = "watchlink-cli";
        a.title = "test";
        a.message = args.at(2).toUtf8().constData();
        link.enqueueAlert(a, QList<settings::WatchEntry>() << entry);
    } else if (cmd == "burst") {
        bool ok = false;
        const int count = args.at(2).toInt(&ok);
        if (!ok || count < 1 || count > kMaxQueued) {
            fprintf(stderr, "N must be 1..%d (the queue limit)\n", kMaxQueued);
            return 2;
        }
        for (int i = 1; i <= count; ++i) {
            notiffilter::WatchNotification a;
            a.category = watchproto::kCategoryGeneric;
            a.sender = "watchlink-cli";
            a.title = "burst";
            a.message = QString("%1/%2").arg(i).arg(count).toUtf8().constData();
            link.enqueueAlert(a, QList<settings::WatchEntry>() << entry);
        }
    }

    out() << "start " << cmd << endl;
    const SessionResult r = link.runSession(req);
    printResult(r);
    return r.ok() ? 0 : 1;
}
