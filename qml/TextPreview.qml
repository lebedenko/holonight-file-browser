pragma ComponentBehavior: Bound
import QtQuick
import Holonight.Core

ListView {
    id: textView
    property int currentLineIndex: -1
    property real columnSpacing: 8
    clip: true
    flickableDirection: Flickable.VerticalFlick
    boundsBehavior: Flickable.StopAtBounds
    keyNavigationEnabled: false
    activeFocusOnTab: false
    TextMetrics {
        id: rowMetrics
        font.family: HolonightTheme.monospaceFont
        font.pointSize: HolonightTheme.monospaceFontSize
        text: "Ag"
    }
    TextMetrics {
        id: gutterMetrics
        font: rowMetrics.font
        text: "0".repeat(String(textView.count).length)
    }
    delegate: Item {
        id: line
        objectName: "quickLookLine"
        required property int index
        required property string lineText
        required property int lineNumber
        readonly property bool isCurrent: line.index === textView.currentLineIndex
        width: ListView.view.width
        height: rowMetrics.height

        Rectangle {
            objectName: "quickLookCurrentLine"
            anchors.fill: parent
            visible: line.isCurrent
            color: HoloniightPalette.surfaceRaised
            radius: HnMetrics.borderWidth * 2
        }

        Text {
            id: numberText
            objectName: "quickLookLineNumbers"
            width: gutterMetrics.width
            horizontalAlignment: Text.AlignRight
            text: line.lineNumber
            textFormat: Text.PlainText
            font: rowMetrics.font
            color: line.isCurrent ? HoloniightPalette.textPrimary : HoloniightPalette.textMuted
        }

        Text {
            objectName: "quickLookLineText"
            x: numberText.width + textView.columnSpacing
            width: line.width - x
            text: line.lineText
            textFormat: Text.PlainText
            wrapMode: Text.NoWrap
            elide: Text.ElideNone
            clip: true
            font: rowMetrics.font
            color: HoloniightPalette.textPrimary
        }
    }
}
