pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls as Controls

GridView {
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    keyNavigationEnabled: false
    activeFocusOnTab: false
    reuseItems: false
    cacheBuffer: Math.max(0, height)
    Controls.ScrollBar.vertical: Controls.ScrollBar {}
}
