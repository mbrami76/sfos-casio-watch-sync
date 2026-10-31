// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

import QtQuick 2.0
import Sailfish.Silica 1.0

// One enabled watch on the Status page. Roles come from the delegate model: title, modelLabel,
// silhouetteId, lastSuccess, nextSync, lastError, problem, running, live, usable, key.
Item {
    id: root

    property string watchKey
    property string title
    property string modelLabel
    property string silhouetteId
    property real lastSuccess
    property real nextSync
    property string lastError
    property string problem
    property bool running
    property bool live
    property bool usable

    width: parent ? parent.width : Screen.width
    height: Math.max(pic.height, info.height) + Theme.paddingLarge

    WatchSilhouette {
        id: pic
        x: Theme.horizontalPageMargin
        y: Theme.paddingSmall
        boxHeight: Theme.itemSizeHuge
        boxWidth: Theme.itemSizeLarge
        silhouetteId: root.silhouetteId
    }

    Column {
        id: info
        x: pic.x + pic.width + Theme.paddingLarge
        width: parent.width - x - Theme.horizontalPageMargin
        spacing: Theme.paddingSmall

        Label {
            width: parent.width
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeLarge
            text: root.title
        }
        Label {
            width: parent.width
            truncationMode: TruncationMode.Fade
            color: Theme.secondaryColor
            text: root.modelLabel
        }
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryColor
            text: qsTr("Last sync: %1").arg(cws.format.when(root.lastSuccess))
        }
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryColor
            visible: root.live && root.usable
            text: qsTr("Next sync: %1").arg(cws.format.next(root.nextSync))
        }
        Row {
            spacing: Theme.paddingSmall
            visible: root.running
            BusyIndicator {
                size: BusyIndicatorSize.ExtraSmall
                running: root.running
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("Syncing...")
            }
        }
        // An error is a word and an icon, never only a colour.
        Row {
            width: parent.width
            spacing: Theme.paddingSmall
            visible: root.lastError !== "" || root.problem !== ""
            Icon {
                id: warn
                source: "image://theme/icon-s-warning"
                anchors.top: parent.top
            }
            Label {
                width: parent.width - warn.width - parent.spacing
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                text: root.problem !== "" ? qsTr("Problem: %1").arg(root.problem)
                                          : qsTr("Error: %1").arg(root.lastError)
            }
        }
        Button {
            text: qsTr("Sync now")
            enabled: cws.service.available && root.live && root.usable && !root.running
            onClicked: cws.service.syncNow(root.watchKey)
        }
    }
}
