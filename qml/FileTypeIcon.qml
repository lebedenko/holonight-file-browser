// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import Holonight.Core

Item {
    id: iconCell
    objectName: "iconColumnField"
    property string iconName
    property bool isDirectory: false
    property real extent: 32
    property var failureCache: null
    property string folderFallback: "qrc:/qt/qml/Holonight/Controls/assets/folder.svg"
    property string fileFallback: "qrc:/qt/qml/Holonight/Controls/assets/generic-file.svg"
    implicitWidth: extent
    implicitHeight: extent

    // Deliberately not reactive to later failures: it only spares rows created after
    // an earlier row's miss from repeating the request (IconFallbacks).
    readonly property bool knownUnresolved: iconCell.failureCache ? iconCell.failureCache.isUnresolved(iconCell.iconName) : false
    // Once this row's own request fails, stop re-requesting (e.g. on a device pixel
    // ratio change) until the row's chain itself changes.
    property string failedChain
    readonly property bool skipRequest: knownUnresolved || failedChain === iconCell.iconName
    readonly property bool showFallback: skipRequest || themeIcon.hasError

    // Theme icon, untinted in its own colours (REQ-F-003). The chain in iconName is
    // walked by the C++ image provider; a total miss surfaces as hasError.
    HnIcon {
        id: themeIcon
        objectName: "themeFileIcon"
        anchors.centerIn: parent
        size: iconCell.extent
        source: iconCell.skipRequest ? "" : "image://icon/" + iconCell.iconName + "?revision=" + HnAppearance.revision
        rendering: HnIcon.Original
        visible: !iconCell.showFallback
        onHasErrorChanged: if (hasError) {
            const chain = iconCell.iconName;
            if (iconCell.failureCache)
                iconCell.failureCache.markUnresolved(chain);
            // Deferred: the failure is reported from inside the source assignment itself.
            Qt.callLater(() => iconCell.failedChain = chain);
        }
    }
    // Bundled glyph, tinted with the palette (REQ-F-016/017); sourced only after a miss.
    HnIcon {
        id: fallbackIcon
        objectName: "fallbackFileIcon"
        anchors.centerIn: parent
        size: iconCell.extent
        source: !iconCell.showFallback ? "" : iconCell.isDirectory ? iconCell.folderFallback : iconCell.fileFallback
        visible: iconCell.showFallback
    }
    // Keep a visible marker even if the packaged SVG cannot be decoded.
    Rectangle {
        objectName: "iconFailurePlaceholder"
        anchors.centerIn: parent
        width: iconCell.extent
        height: iconCell.extent
        radius: 3
        color: "transparent"
        border.color: HoloniightPalette.textMuted
        visible: iconCell.showFallback && fallbackIcon.hasError
        Text {
            anchors.centerIn: parent
            text: "?"
            color: HoloniightPalette.textMuted
            font.pixelSize: 14
        }
    }
}
