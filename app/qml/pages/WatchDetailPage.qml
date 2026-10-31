// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

import QtQuick 2.0
import Sailfish.Silica 1.0
import "../components"
import "../components/texts.js" as Texts

// Watch detail. The BLE address is not shown (privacy).
Page {
    id: page
    property string watchKey
    readonly property var watch: cws.watches.item(watchKey)

    allowedOrientations: Orientation.All

    WatchActions { id: actions }

    RemorsePopup { id: remorse }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Forget this watch")
                visible: page.watch.inSettings
                onClicked: remorse.execute(qsTr("Forgetting this watch"), function() {
                    cws.watches.forget(page.watchKey)
                    pageStack.pop()
                })
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader { title: page.watch.title }

            TextField {
                id: aliasField
                width: parent.width
                label: qsTr("Alias")
                placeholderText: qsTr("Alias")
                text: page.watch.alias
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
                onActiveFocusChanged: {
                    if (!activeFocus && text !== page.watch.alias)
                        cws.watches.setAlias(page.watchKey, text)
                }
            }

            DetailItem {
                label: qsTr("Model")
                value: page.watch.modelLabel
            }
            DetailItem {
                label: qsTr("Support")
                value: Texts.supportLong(page.watch.supportStatus)
            }
            DetailItem {
                label: qsTr("Found now")
                visible: page.watch.found >= 0
                value: page.watch.found === 1 ? qsTr("Yes") : qsTr("No")
            }

            TextSwitch {
                automaticCheck: false
                checked: page.watch.enabled
                text: qsTr("Enabled")
                enabled: actions.canToggle(page.watch.supportStatus, page.watch.enabled)
                onClicked: actions.toggle(page.watchKey, page.watch.enabled,
                                          page.watch.supportStatus, page.watch.riskAcknowledged)
            }

            // Silhouette: opens the picker.
            BackgroundItem {
                width: parent.width
                height: Theme.itemSizeHuge
                onClicked: pageStack.push(Qt.resolvedUrl("SilhouettePage.qml"),
                                          { watchKey: page.watchKey })
                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Silhouette")
                }
                WatchSilhouette {
                    anchors.right: arrow.left
                    anchors.rightMargin: Theme.paddingLarge
                    anchors.verticalCenter: parent.verticalCenter
                    boxHeight: Theme.itemSizeLarge
                    boxWidth: Theme.itemSizeMedium
                    silhouetteId: page.watch.silhouetteId
                }
                Icon {
                    id: arrow
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-m-right"
                }
            }

            Item { width: 1; height: Theme.paddingMedium }

            DetailItem {
                label: qsTr("Last sync")
                value: cws.format.when(page.watch.lastSuccess)
            }
            DetailItem {
                label: qsTr("Last contact")
                value: page.watch.lastOpTime > 0
                       ? cws.format.when(page.watch.lastOpTime)
                         + (Texts.lastOpKind(page.watch.lastOpKind) !== ""
                            ? " (" + Texts.lastOpKind(page.watch.lastOpKind) + ")" : "")
                       : qsTr("Never")
            }
            DetailItem {
                label: qsTr("Last error")
                value: page.watch.lastError !== "" ? page.watch.lastError : qsTr("none")
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Sync now")
                enabled: cws.service.available && page.watch.usable && !page.watch.running
                onClicked: cws.service.syncNow(page.watchKey)
            }

            // ---- WIZARD ENTRY POINT (R-GEN-6) ------------------------------------- The
            // compatibility check ("Run compatibility check": for an untested model, under the
            // pull-down for a supported one) goes here. The wizard is not built yet: no
            // page and no button are built yet. The at-your-own-risk confirmation stays
            // (RiskDialog). -----------------------------------------------------------------------

            Item { width: 1; height: Theme.paddingLarge }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryColor
                text: qsTr("Forgetting a watch removes it from this app only. The Bluetooth pairing stays; remove it in the system Bluetooth settings.")
            }
        }
        VerticalScrollDecorator { }
    }
}
