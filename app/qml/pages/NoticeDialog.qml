// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components/texts.js" as Texts

// Shown the first time the master switch is switched on.
Dialog {
    allowedOrientations: Orientation.All

    onAccepted: {
        cws.settings.acceptNotificationNotice()
        cws.settings.masterOn = true
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            DialogHeader {
                acceptText: qsTr("Accept")
                cancelText: qsTr("Cancel")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: Texts.noticeText()
            }
        }
        VerticalScrollDecorator { }
    }
}
