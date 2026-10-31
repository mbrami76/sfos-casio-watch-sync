// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// One binary, two modes: `--service` runs the headless service; without it the UI starts. The UI
// is single instance (InstanceGuard): a second start brings the running window forward and exits.
#include "servicemain.h"

#include <QCoreApplication>
#include <QStringList>

#include <stdio.h>
#include <string.h>

#ifdef WITH_UI
#include "instanceguard.h"
#include "uibridge.h"

#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickView>
#include <QScopedPointer>
#include <sailfishapp.h>

static int runUi(int argc, char *argv[], const QString &modelsFile)
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    QCoreApplication::setOrganizationName(QStringLiteral("it.michelebrami"));

    uibridge::InstanceGuard guard;
    if (!guard.claim())
        return 0;       // the running instance was asked to come forward

    uibridge::UiPaths paths;
    paths.modelsFallbackPath = modelsFile;
    paths.silhouettesDir = SailfishApp::pathTo(QStringLiteral("images/silhouettes")).toLocalFile();
    uibridge::UiBridge bridge(paths);

    QScopedPointer<QQuickView> view(SailfishApp::createView());
    view->rootContext()->setContextProperty(QStringLiteral("cws"), &bridge);
    view->setSource(SailfishApp::pathTo(QStringLiteral("qml/sfos-casio-sync.qml")));
    view->show();

    QObject::connect(&guard, &uibridge::InstanceGuard::activateRequested, view.data(), [&view]() {
        view->show();
        view->raise();
        view->requestActivate();
        // Silica's ApplicationWindow.activate() asks the compositor to show the window.
        if (view->rootObject())
            QMetaObject::invokeMethod(view->rootObject(), "activate");
    });
    return app->exec();
}
#endif

int main(int argc, char *argv[])
{
    // The mode is decided from argv before an application object exists: the service needs a
    // QCoreApplication, the UI a QGuiApplication.
    bool serviceMode = false;
    QString modelsFile;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--service") == 0) {
            serviceMode = true;
        } else if (strcmp(argv[i], "--models") == 0 && i + 1 < argc) {
            modelsFile = QString::fromLocal8Bit(argv[++i]);
        } else {
            fprintf(stderr, "Usage: sfos-casio-sync [--service] [--models FILE]\n");
            return 2;
        }
    }

    if (serviceMode) {
        QCoreApplication app(argc, argv);
        QCoreApplication::setApplicationName(QStringLiteral("sfos-casio-sync"));
        QCoreApplication::setOrganizationName(QStringLiteral("it.michelebrami"));
        return service::runService(modelsFile);
    }

#ifdef WITH_UI
    return runUi(argc, argv, modelsFile);
#else
    fprintf(stderr, "Casio Watch Sync: this build has no user interface. "
                    "Run with --service for the background service.\n");
    return 0;
#endif
}
