// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound
import QtQuick

QtObject {
    property real availableWidth: 0
    property real gutterWidth: 0
    property real selectionWidth: 0
    property real spacing: 12
    readonly property bool showModified: availableWidth >= 640
    readonly property bool showSize: availableWidth >= 420
    readonly property real modifiedWidth: showModified ? 150 : 0
    readonly property real sizeWidth: showSize ? 90 : 0
    readonly property real nameWidth: Math.max(0, availableWidth - gutterWidth - selectionWidth - modifiedWidth - sizeWidth - spacing * ((gutterWidth > 0 ? 1 : 0) + (selectionWidth > 0 ? 1 : 0) + (showSize ? 1 : 0) + (showModified ? 1 : 0)))
}
