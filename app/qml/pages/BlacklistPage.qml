// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

import QtQuick 2.0
import Sailfish.Silica 1.0

// Blacklist: plain substring of the app name, not case sensitive, no wildcards; it wins over an
// enabled app.
Page {
    id: page
    allowedOrientations: Orientation.All

    function addEntry() {
        if (cws.settings.addBlacklist(addField.text))
            addField.text = ""
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        model: cws.settings.blacklist

        header: Column {
            width: list.width
            PageHeader { title: qsTr("Blacklist") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Never forwarded and never listed. Text, part of the app name; not case sensitive.")
            }
            Row {
                width: parent.width
                TextField {
                    id: addField
                    width: parent.width - addButton.width
                    placeholderText: qsTr("Add text...")
                    label: qsTr("Add text")
                    EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                    EnterKey.onClicked: page.addEntry()
                }
                IconButton {
                    id: addButton
                    anchors.verticalCenter: parent.verticalCenter
                    icon.source: "image://theme/icon-m-add"
                    enabled: addField.text.trim() !== ""
                    onClicked: page.addEntry()
                }
            }
        }

        delegate: ListItem {
            contentHeight: Theme.itemSizeSmall
            Label {
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: del.x - x - Theme.paddingMedium
                truncationMode: TruncationMode.Fade
                text: modelData
            }
            IconButton {
                id: del
                anchors.right: parent.right
                anchors.rightMargin: Theme.horizontalPageMargin - Theme.paddingLarge
                anchors.verticalCenter: parent.verticalCenter
                icon.source: "image://theme/icon-m-delete"
                onClicked: cws.settings.removeBlacklist(index)
            }
        }
        VerticalScrollDecorator { }
    }
}
