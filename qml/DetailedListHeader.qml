// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core

RowLayout {
    id: root
    required property DetailedColumnLayout columns
    signal sortRequested(string column)
    spacing: columns.spacing
    Item {
        visible: root.columns.gutterWidth > 0
        Layout.preferredWidth: root.columns.gutterWidth
    }
    Item {
        visible: root.columns.selectionWidth > 0
        Layout.preferredWidth: root.columns.selectionWidth
    }
    Controls.ToolButton {
        id: nameButton
        leftPadding: 8
        contentItem: HnLabel {
            objectName: "nameColumnHeader"
            rawText: nameButton.text
            elide: Text.ElideRight
        }
        text: qsTr("Name")
        Layout.preferredWidth: root.columns.nameWidth
        Layout.minimumWidth: 0
        onClicked: root.sortRequested("name")
    }
    Controls.ToolButton {
        objectName: "sizeColumnHeader"
        text: qsTr("Size")
        visible: root.columns.showSize
        Layout.preferredWidth: root.columns.sizeWidth
        Layout.minimumWidth: 0
        Layout.maximumWidth: root.columns.sizeWidth
        onClicked: root.sortRequested("size")
    }
    Controls.ToolButton {
        objectName: "modifiedColumnHeader"
        text: qsTr("Modified")
        visible: root.columns.showModified
        Layout.preferredWidth: root.columns.modifiedWidth
        Layout.minimumWidth: 0
        Layout.maximumWidth: root.columns.modifiedWidth
        onClicked: root.sortRequested("modified")
    }
}
