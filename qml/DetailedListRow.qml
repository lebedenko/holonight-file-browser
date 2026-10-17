// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

HnListDelegate {
    id: root
    required property DetailedColumnLayout columns
    property string filename: text
    property string sizeText
    property string modifiedText
    property Component gutterContent
    property Component selectionContent
    property Component filenameContent
    property Component editorContent
    property bool editing: false
    readonly property alias editorItem: editor.item
    Accessible.name: filename
    Accessible.description: sizeText + (modifiedText.length ? ", " + modifiedText : "")
    padding: 0
    contentItem: RowLayout {
        spacing: root.columns.spacing
        Loader {
            sourceComponent: root.gutterContent
            visible: root.columns.gutterWidth > 0
            Layout.preferredWidth: root.columns.gutterWidth
        }
        Loader {
            sourceComponent: root.selectionContent
            visible: root.columns.selectionWidth > 0
            Layout.preferredWidth: root.columns.selectionWidth
        }
        Loader {
            id: editor
            sourceComponent: root.editing ? root.editorContent : (root.filenameContent || defaultName)
            Layout.preferredWidth: root.columns.nameWidth
            Layout.minimumWidth: 0
        }
        HnLabel {
            objectName: "sizeColumnField"
            rawText: root.sizeText
            elide: Text.ElideRight
            visible: root.columns.showSize
            Layout.preferredWidth: root.columns.sizeWidth
            Layout.minimumWidth: 0
            Layout.maximumWidth: root.columns.sizeWidth
            Accessible.ignored: true
        }
        HnLabel {
            objectName: "modifiedColumnField"
            rawText: root.modifiedText
            elide: Text.ElideRight
            visible: root.columns.showModified
            Layout.preferredWidth: root.columns.modifiedWidth
            Layout.minimumWidth: 0
            Layout.maximumWidth: root.columns.modifiedWidth
            Accessible.ignored: true
        }
    }
    Component {
        id: defaultName
        HnLabel {
            rawText: root.filename
            elide: Text.ElideRight
            Accessible.ignored: true
        }
    }
}
