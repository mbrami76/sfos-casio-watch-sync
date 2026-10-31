// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// Notifications. Never shows notification text: app names only.
Page {
    id: page
    allowedOrientations: Orientation.All

    property string testResult
    readonly property bool monitorBlocked: cws.settings.masterOn && cws.service.statusKnown
                                           && cws.service.monitorState === "cannot-monitor"

    Connections {
        target: cws.service
        onTestNotificationResult: page.testResult = sent
            ? qsTr("Test notification sent to the watch.")
            : qsTr("No watch can receive the test notification.")
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: cws.apps

        PullDownMenu {
            MenuItem {
                text: qsTr("Send test notification")
                enabled: cws.service.available
                onClicked: {
                    page.testResult = ""
                    cws.service.testNotification()
                }
            }
        }

        header: Column {
            width: list.width
            PageHeader { title: qsTr("Notifications") }
            MasterSwitch { enabled: true }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Forwarded notifications always vibrate on the watch.")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                visible: page.monitorBlocked
                text: cws.service.monitorReason !== ""
                      ? qsTr("Cannot read notifications: %1").arg(cws.service.monitorReason)
                      : qsTr("Cannot read notifications")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                visible: page.testResult !== ""
                text: page.testResult
            }
            SectionHeader { text: qsTr("Apps") }
        }

        delegate: ListItem {
            contentHeight: Theme.itemSizeSmall
            menu: Component {
                ContextMenu {
                    MenuItem {
                        text: qsTr("Remove from list")
                        onClicked: cws.apps.remove(index)
                    }
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: toggle.x - x - Theme.paddingMedium
                truncationMode: TruncationMode.Fade
                text: model.name
            }
            Switch {
                id: toggle
                anchors.right: parent.right
                anchors.rightMargin: Theme.horizontalPageMargin - Theme.paddingLarge
                anchors.verticalCenter: parent.verticalCenter
                automaticCheck: false
                enabled: !page.monitorBlocked
                checked: model.enabled
                onClicked: cws.apps.setEnabled(index, !model.enabled)
            }
        }

        footer: Column {
            width: list.width
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                visible: cws.apps.count === 0
                text: qsTr("Apps appear here after they show their first notification, once forwarding is on.")
            }
            BackgroundItem {
                width: parent.width
                onClicked: pageStack.push(Qt.resolvedUrl("BlacklistPage.qml"))
                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Blacklist (%1)").arg(cws.settings.blacklist.length)
                }
                Icon {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-m-right"
                }
            }
        }
        VerticalScrollDecorator { }
    }
}
