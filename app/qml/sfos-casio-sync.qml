// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

import QtQuick 2.0
import Sailfish.Silica 1.0
import "pages"
import "cover"

ApplicationWindow {
    id: window

    initialPage: Component { StatusPage { } }
    cover: Component { CoverPage { } }
    allowedOrientations: defaultAllowedOrientations

    // The window is brought back (or the single instance is asked to come
    // forward): re-read the settings file and the status.
    Connections {
        target: Qt.application
        onStateChanged: {
            if (Qt.application.state === Qt.ApplicationActive)
                cws.refresh()
        }
    }
}
