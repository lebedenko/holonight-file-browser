// SPDX-License-Identifier: GPL-3.0-or-later

pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import Holonight.Core
import Holonight.Controls

Controls.ScrollView {
    id: root
    contentWidth: availableWidth
    clip: true
    required property var service
    property string iconProvider: "image://icon/"
    // Formatting is deliberately supplied by the consumer.
    property var metadataRows: []
    readonly property real previewDevicePixelRatio: Window.window ? Window.window.devicePixelRatio : 1
    function reportSize(): void {
        if (service && frame)
            service.setRequestedSize(0, Qt.size(frame.width * previewDevicePixelRatio, frame.height * previewDevicePixelRatio));
    }
    onPreviewDevicePixelRatioChanged: reportSize()
    onServiceChanged: reportSize()
    ColumnLayout {
        width: root.availableWidth
        HnPreviewFrame {
            id: frame
            objectName: "filePreviewFrame"
            onWidthChanged: root.reportSize()
            onHeightChanged: root.reportSize()
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(160, width * 0.75)
            previewState: !root.service || !root.service.hasEntry ? HnPreviewFrame.Empty : root.service.hasImage ? HnPreviewFrame.Content : root.service.busy ? HnPreviewFrame.Loading : root.service.previewErrorMessage.length ? HnPreviewFrame.Error : HnPreviewFrame.Content
            previewContent: Item {
                PreviewImageItem {
                    anchors.fill: parent
                    visible: root.service && root.service.hasImage
                    image: root.service ? root.service.image : null
                }
                HnIcon {
                    id: contentIcon
                    anchors.centerIn: parent
                    size: 64
                    visible: root.service && !root.service.hasImage
                    source: root.service ? root.iconProvider + root.service.iconName : ""
                    rendering: HnIcon.Original
                }
                HnIcon {
                    anchors.centerIn: parent
                    size: 64
                    visible: contentIcon.visible && contentIcon.hasError
                    source: "qrc:/qt/qml/Holonight/Controls/assets/generic-file.svg"
                }
            }
            loadingContent: Item {
                Controls.BusyIndicator {
                    anchors.centerIn: parent
                    running: true
                    Accessible.name: qsTr("Loading preview")
                }
            }
            errorContent: Item {
                Accessible.name: qsTr("No preview available")
                Accessible.description: root.service ? root.service.previewErrorMessage : ""
                HoverHandler {
                    id: failureHover
                }
                Controls.ToolTip.visible: failureHover.hovered
                Controls.ToolTip.text: root.service ? root.service.previewErrorMessage : ""
                ColumnLayout {
                    anchors.centerIn: parent
                    width: parent.width
                    HnIcon {
                        id: errorIcon
                        objectName: "filePreviewErrorIcon"
                        Layout.alignment: Qt.AlignHCenter
                        size: 64
                        source: root.service ? root.iconProvider + root.service.iconName : ""
                        rendering: HnIcon.Original
                        visible: !hasError
                    }
                    HnIcon {
                        Layout.alignment: Qt.AlignHCenter
                        size: 64
                        objectName: "filePreviewFallbackIcon"
                        visible: errorIcon.hasError
                        source: "qrc:/qt/qml/Holonight/Controls/assets/generic-file.svg"
                    }
                    HnLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        role: HnTypographyRole.Caption
                        color: HoloniightPalette.textMuted
                        objectName: "filePreviewFailureCaption"
                        rawText: qsTr("No preview available")
                    }
                }
            }
        }
        HnMetadataList {
            Layout.fillWidth: true
            rows: root.metadataRows
        }
    }
}
