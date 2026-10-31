// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

import QtQuick 2.0
import Sailfish.Silica 1.0

// Settings. The interval is a choice from the allowed list.
Page {
    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader { title: qsTr("Settings") }

            ComboBox {
                id: interval
                label: qsTr("Sync interval")
                currentIndex: cws.settings.intervalIndex
                menu: ContextMenu {
                    Repeater {
                        model: cws.settings.intervalChoices
                        MenuItem { text: modelData.label }
                    }
                }
                // Silica's ComboBox has no "activated" signal here: act on a real change only.
                onCurrentIndexChanged: {
                    var minutes = cws.settings.intervalChoices[currentIndex].minutes
                    if (minutes !== cws.settings.intervalMinutes)
                        cws.settings.intervalMinutes = minutes
                }
                Connections {
                    target: cws.settings
                    onIntervalChanged: interval.currentIndex = cws.settings.intervalIndex
                }
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("The time comes from the phone clock. The app uses no network.")
            }

            Item { width: 1; height: Theme.paddingLarge }

            TextSwitch {
                automaticCheck: false
                checked: cws.settings.debugLog
                text: qsTr("Debug log")
                description: qsTr("Metadata only, never the text of a notification.")
                onClicked: cws.settings.debugLog = !cws.settings.debugLog
            }
        }
        VerticalScrollDecorator { }
    }
}
