// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

.pragma library

// Words for the support status of a model.
function support(status) {
    if (status === "supported")
        return qsTr("Supported")
    if (status === "untested")
        return qsTr("Untested")
    return qsTr("Unknown model")
}

function supportLong(status) {
    if (status === "supported")
        return qsTr("Supported")
    if (status === "untested")
        return qsTr("Untested (at your own risk)")
    return qsTr("Unknown model")
}

// found: 1 found now, 0 not found, -1 not known (no list from the service yet)
function foundSuffix(found) {
    return found === 0 ? qsTr("Not found") : ""
}

function lastOpKind(kind) {
    if (kind === "timesync")
        return qsTr("time sync")
    if (kind === "notif")
        return qsTr("notification")
    return ""
}

// The first-switch-on notice and the About statement share it.
function noticeText() {
    return qsTr("Casio Watch Sync will read all notifications on this phone to forward the ones you choose. Nothing leaves the phone except the text of the notifications you enable, sent to your watch. Continue?")
}

function notAffiliatedText() {
    return qsTr("Casio Watch Sync is an unofficial application. It is not affiliated with, endorsed by or sponsored by Casio Computer Co., Ltd. Casio and the names of its products belong to their owners.")
}
