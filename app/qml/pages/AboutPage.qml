// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components/texts.js" as Texts

// About.
Page {
    allowedOrientations: Orientation.All

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader { title: qsTr("About") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraLarge
                color: Theme.highlightColor
                text: qsTr("Casio Watch Sync (CWS)")
            }
            DetailItem {
                label: qsTr("Version")
                value: cws.about.version
            }
            DetailItem {
                label: qsTr("Licence")
                value: cws.about.license
            }
            DetailItem {
                label: qsTr("Project")
                value: cws.about.address
            }

            SectionHeader { text: qsTr("Notifications") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                text: qsTr("This app is distributed outside Harbour. It reads all notifications on this phone to forward the ones you enable to your watch. Nothing leaves the phone except the text of those notifications, sent to your watch. The app makes no network connections.")
            }

            SectionHeader { text: qsTr("Not affiliated") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                text: Texts.notAffiliatedText()
            }
        }
        VerticalScrollDecorator { }
    }
}
