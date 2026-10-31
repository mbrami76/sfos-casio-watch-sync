// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

import QtQuick 2.0
import Sailfish.Silica 1.0

// The enable switch of a watch, shared by the Watches page and Watch detail: an untested model asks
// first, an unknown one cannot be enabled.
QtObject {
    function canToggle(supportStatus, enabled) {
        return enabled || supportStatus !== "unknown"
    }

    function toggle(key, enabled, supportStatus, riskAcknowledged) {
        if (enabled) {
            cws.watches.setEnabled(key, false)
        } else if (supportStatus === "supported" || riskAcknowledged) {
            cws.watches.setEnabled(key, true)
        } else if (supportStatus === "untested") {
            pageStack.push(Qt.resolvedUrl("../pages/RiskDialog.qml"), { watchKey: key })
        }
    }
}
