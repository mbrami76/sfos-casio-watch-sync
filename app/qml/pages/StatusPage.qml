// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// Home page.
Page {
    id: page
    allowedOrientations: Orientation.All

    onStatusChanged: {
        if (status === PageStatus.Active)
            cws.refresh()
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Sync all now")
                enabled: cws.service.available && cws.watches.enabledCount > 0
                onClicked: cws.service.syncAll()
            }
            MenuItem {
                text: qsTr("Watches")
                onClicked: pageStack.push(Qt.resolvedUrl("WatchesPage.qml"))
            }
            MenuItem {
                text: qsTr("Notifications")
                onClicked: pageStack.push(Qt.resolvedUrl("NotificationsPage.qml"))
            }
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
        }

        PushUpMenu {
            MenuItem {
                text: qsTr("About")
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader { title: qsTr("Casio Watch Sync") }

            // The service line; the service may be absent, the page keeps working on settings.
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                text: cws.service.available ? qsTr("Service: running") : qsTr("Service: not running")
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !cws.service.available
                text: qsTr("Start")
                onClicked: cws.service.startService()
            }

            Item { width: 1; height: Theme.paddingLarge }

            // Empty state (no enabled watch).
            Column {
                width: parent.width
                visible: cws.watches.enabledCount === 0
                spacing: Theme.paddingMedium
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: Theme.fontSizeExtraLarge
                    color: Theme.highlightColor
                    wrapMode: Text.Wrap
                    text: qsTr("No watch yet")
                }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryHighlightColor
                    wrapMode: Text.Wrap
                    text: qsTr("Pull down and open Watches")
                }
            }

            Repeater {
                model: cws.enabledWatches
                delegate: WatchBlock {
                    watchKey: model.key
                    title: model.title
                    modelLabel: model.modelLabel
                    silhouetteId: model.silhouetteId
                    lastSuccess: model.lastSuccess
                    nextSync: model.nextSync
                    lastError: model.lastError
                    problem: model.problem
                    running: model.running
                    live: model.live
                    usable: model.usable
                }
            }

            Item { width: 1; height: Theme.paddingLarge }

            MasterSwitch { }
        }
        VerticalScrollDecorator { }
    }
}
