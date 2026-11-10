// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import Holonight.Core

HnLabel {
    id: root
    required property int rowIndex
    required property int cursorIndex
    readonly property bool isCursorRow: rowIndex === cursorIndex
    objectName: "lineNumberGutterField"
    role: HnTypographyRole.Code
    textFormat: Text.PlainText
    font.weight: isCursorRow ? Font.Bold : Font.Normal
    rawText: isCursorRow ? String(rowIndex + 1) : String(Math.abs(rowIndex - cursorIndex))
    color: isCursorRow ? HoloniightPalette.accentViolet : HoloniightPalette.textMuted
    horizontalAlignment: isCursorRow ? Text.AlignLeft : Text.AlignRight
    verticalAlignment: Text.AlignVCenter
    leftPadding: 8
    rightPadding: 8
    Accessible.ignored: true
}
