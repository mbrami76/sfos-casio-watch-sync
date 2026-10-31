// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

import QtQuick 2.0
import Sailfish.Silica 1.0

// Application cover. Ambience colours set by the OS: the background is the ambience background, the
// classic silhouette is rotated 20 degrees clockwise, scaled to fill the cover and drawn in
// the ambience foreground colour at reduced opacity; the text uses the same foreground. Nothing
// here depends on a colour for meaning.
CoverBackground {
    id: cover

    // A w x h image rotated by 20 degrees covers W x H without empty corners if
    // w >= W cos20 + H sin20 and h >= W sin20 + H cos20.
    readonly property real _cos: 0.9397
    readonly property real _sin: 0.3420
    readonly property real _aspect: {
        var a = cws.silhouettes.aspectOf("classic")
        return a > 0 ? a : 0.477
    }
    readonly property real _needW: width * _cos + height * _sin
    readonly property real _needH: Math.max(width * _sin + height * _cos, _needW / _aspect)

    Rectangle {
        anchors.fill: parent
        color: Theme.highlightBackgroundColor
        opacity: Theme.highlightBackgroundOpacity
    }

    Item {
        anchors.fill: parent
        clip: true

        Icon {
            anchors.centerIn: parent
            rotation: 20
            height: cover._needH
            width: height * cover._aspect
            sourceSize.width: width
            sourceSize.height: height
            fillMode: Image.PreserveAspectFit
            source: cws.silhouettes.sourceOf("classic")
            color: Theme.primaryColor
            opacity: 0.15
        }
    }

    Label {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: Theme.primaryColor
        text: cws.lastSyncLine
    }

    CoverActionList {
        CoverAction {
            iconSource: "image://theme/icon-cover-sync"
            onTriggered: cws.service.syncAll()
        }
    }
}
