// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

import QtQuick 2.0
import Sailfish.Silica 1.0

// Enabling an untested model (at the user's own risk).
Dialog {
    property string watchKey

    allowedOrientations: Orientation.All

    onAccepted: cws.watches.enableAtOwnRisk(watchKey)

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            DialogHeader {
                acceptText: qsTr("Enable at my risk")
                cancelText: qsTr("Cancel")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: qsTr("This model has not been tested with this app. It may not work correctly. Use it at your own risk. A compatibility check takes about 5 minutes and is recommended.")
            }
        }
        VerticalScrollDecorator { }
    }
}
