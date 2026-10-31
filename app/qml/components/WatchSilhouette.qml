// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

import QtQuick 2.0
import Sailfish.Silica 1.0

// A watch silhouette: fitted into a box, centred, aspect kept, drawn in the ambience foreground
// colour. The files are black on transparent and sized in millimetres, so sourceSize is set
// explicitly.
Item {
    id: root

    property string silhouetteId: "classic"
    property real boxHeight: Theme.itemSizeLarge
    property real boxWidth: boxHeight
    property color color: Theme.primaryColor

    readonly property real _aspect: {
        var a = cws.silhouettes.aspectOf(silhouetteId)
        return a > 0 ? a : 0.5
    }
    readonly property real _fitWidth: Math.min(boxWidth, boxHeight * _aspect)

    width: boxWidth
    height: boxHeight

    Icon {
        anchors.centerIn: parent
        width: root._fitWidth
        height: root._fitWidth / root._aspect
        sourceSize.width: width
        sourceSize.height: height
        fillMode: Image.PreserveAspectFit
        source: cws.silhouettes.sourceOf(root.silhouetteId)
        color: root.color
    }
}
