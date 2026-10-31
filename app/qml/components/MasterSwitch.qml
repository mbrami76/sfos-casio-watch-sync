// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

import QtQuick 2.0
import Sailfish.Silica 1.0

// The master switch of notification forwarding (Status and Notifications pages). The first time it
// goes on, the notice of is shown.
TextSwitch {
    id: root

    readonly property string monitorState: cws.service.monitorState
    readonly property bool cannotMonitor: cws.settings.masterOn && cws.service.statusKnown
                                          && (monitorState === "cannot-monitor")

    automaticCheck: false
    checked: cws.settings.masterOn
    text: qsTr("Forward notifications")
    description: {
        if (!cws.settings.masterOn)
            return ""
        if (!cws.service.available)
            return qsTr("Service not running")
        if (!cws.service.statusKnown)
            return ""
        if (monitorState === "cannot-monitor")
            return cws.service.monitorReason !== ""
                    ? qsTr("Cannot read notifications: %1").arg(cws.service.monitorReason)
                    : qsTr("Cannot read notifications")
        if (monitorState === "running")
            return qsTr("Monitoring active")
        return qsTr("Monitoring not running")
    }

    onClicked: {
        if (cws.settings.masterOn) {
            cws.settings.masterOn = false
        } else if (cws.settings.notificationNoticeNeeded()) {
            pageStack.push(Qt.resolvedUrl("../pages/NoticeDialog.qml"))
        } else {
            cws.settings.masterOn = true
        }
    }
}
