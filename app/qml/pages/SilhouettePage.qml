// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"

// Silhouette picker: the user chooses the picture of each watch, whatever the real model. The
// selected one carries a check and the word "Selected", not only a colour.
Page {
    id: page
    property string watchKey
    readonly property var watch: cws.watches.item(watchKey)

    allowedOrientations: Orientation.All

    SilicaGridView {
        id: grid
        anchors.fill: parent
        model: cws.silhouettes
        cellWidth: width / (page.isLandscape ? 4 : 2)
        cellHeight: Theme.itemSizeHuge * 2

        header: PageHeader { title: qsTr("Silhouette") }

        delegate: BackgroundItem {
            id: tile
            readonly property bool selected: model.silhouetteId === page.watch.silhouetteId
            width: grid.cellWidth
            height: grid.cellHeight
            highlighted: down || selected

            WatchSilhouette {
                anchors.horizontalCenter: parent.horizontalCenter
                y: Theme.paddingLarge
                boxHeight: Theme.itemSizeHuge + Theme.itemSizeMedium
                boxWidth: tile.width - 2 * Theme.paddingLarge
                silhouetteId: model.silhouetteId
            }
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.paddingMedium
                spacing: Theme.paddingSmall
                Icon {
                    visible: tile.selected
                    source: "image://theme/icon-s-installed"
                    anchors.verticalCenter: parent.verticalCenter
                }
                Label {
                    text: tile.selected ? qsTr("%1 (Selected)").arg(model.label) : model.label
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
            onClicked: {
                cws.watches.setSilhouette(page.watchKey, model.silhouetteId)
                pageStack.pop()
            }
        }
        VerticalScrollDecorator { }
    }
}
