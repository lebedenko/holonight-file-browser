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
            onWidthChanged: root.reportSize()
            onHeightChanged: root.reportSize()
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(160, width * 0.75)
            previewState: !root.service || !root.service.hasEntry ? HnPreviewFrame.Empty : root.service.busy ? HnPreviewFrame.Loading : root.service.previewErrorMessage.length ? HnPreviewFrame.Error : HnPreviewFrame.Content
            previewContent: Item {
                PreviewImageItem {
                    anchors.fill: parent
                    visible: root.service && root.service.hasImage
                    image: root.service ? root.service.image : null
                }
                HnIcon {
                    anchors.centerIn: parent
                    size: 64
                    visible: root.service && !root.service.hasImage
                    source: root.service ? root.iconProvider + root.service.iconName : ""
                    rendering: HnIcon.Original
                }
            }
            emptyContent: HnEmptyState {
                titleText: qsTr("Select a file")
            }
            loadingContent: HnLoadingState {
                titleText: qsTr("Loading preview")
            }
            errorContent: ColumnLayout {
                HnIcon {
                    Layout.alignment: Qt.AlignHCenter
                    size: 64
                    source: root.service ? root.iconProvider + root.service.iconName : ""
                    rendering: HnIcon.Original
                }
                HnEmptyState {
                    Layout.fillWidth: true
                    titleText: qsTr("Preview unavailable")
                    descriptionText: root.service ? root.service.previewErrorMessage : ""
                }
            }
        }
        HnMetadataList {
            Layout.fillWidth: true
            rows: root.metadataRows
        }
    }
}
