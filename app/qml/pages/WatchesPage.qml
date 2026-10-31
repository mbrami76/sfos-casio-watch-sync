// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"
import "../components/texts.js" as Texts

// Watches: the settings list in session order, then the paired devices of the model table that are
// not in it yet.
Page {
    id: page
    allowedOrientations: Orientation.All

    WatchActions { id: actions }

    Component.onCompleted: cws.service.listWatches()

    SilicaListView {
        id: list
        anchors.fill: parent
        model: cws.watches

        header: PageHeader {
            title: qsTr("Watches")
        }

        PullDownMenu {
            busy: cws.service.listing
            MenuItem {
                text: qsTr("Refresh list")
                enabled: cws.service.available
                onClicked: cws.service.listWatches()
            }
        }

        delegate: ListItem {
            id: item
            contentHeight: Theme.itemSizeHuge
            menu: Component {
                ContextMenu {
                    MenuItem {
                        text: qsTr("Move up")
                        visible: cws.watches.canMoveUp(index)
                        onClicked: cws.watches.moveUp(index)
                    }
                    MenuItem {
                        text: qsTr("Move down")
                        visible: cws.watches.canMoveDown(index)
                        onClicked: cws.watches.moveDown(index)
                    }
                }
            }
            onClicked: pageStack.push(Qt.resolvedUrl("WatchDetailPage.qml"), { watchKey: model.key })

            WatchSilhouette {
                id: pic
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                boxHeight: Theme.itemSizeLarge
                boxWidth: Theme.itemSizeMedium
                silhouetteId: model.silhouetteId
            }
            Column {
                x: pic.x + pic.width + Theme.paddingLarge
                width: toggle.x - x - Theme.paddingMedium
                anchors.verticalCenter: parent.verticalCenter
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    text: model.title
                }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryColor
                    text: model.modelLabel
                }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryColor
                    text: {
                        var s = Texts.support(model.supportStatus)
                        var f = Texts.foundSuffix(model.found)
                        return f !== "" ? s + ", " + f : s
                    }
                }
            }
            Switch {
                id: toggle
                anchors.right: parent.right
                anchors.rightMargin: Theme.horizontalPageMargin - Theme.paddingLarge
                anchors.verticalCenter: parent.verticalCenter
                automaticCheck: false
                checked: model.enabled
                enabled: actions.canToggle(model.supportStatus, model.enabled)
                onClicked: actions.toggle(model.key, model.enabled, model.supportStatus,
                                          model.riskAcknowledged)
            }
        }

        footer: Column {
            width: list.width
            spacing: Theme.paddingMedium

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                size: BusyIndicatorSize.Small
                running: cws.service.listing
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                visible: !cws.service.available
                text: qsTr("The service is not running, so paired devices cannot be listed.")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                visible: cws.service.listError !== ""
                text: qsTr("Error: %1").arg(cws.service.listError)
            }
            // Empty state
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                visible: cws.watches.count === 0 && !cws.service.listing
                text: qsTr("No paired Casio watch found. Pair your watch in the system Bluetooth settings, then pull down to refresh.")
            }
            // Link to the system Bluetooth settings (decision 2).
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: cws.watches.count === 0 && !cws.service.listing
                text: qsTr("Open Bluetooth settings")
                onClicked: cws.about.openBluetoothSettings()
            }
            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: !cws.service.available
                text: qsTr("Start service")
                onClicked: cws.service.startService()
            }
        }

        VerticalScrollDecorator { }
    }
}
