import QtQuick
import QtQuick.Controls.Basic
import Drift

// VEDIT: CapCut-style tab bar across the top of a side panel — icon over label, the current tab
// in the accent colour with a line under it. Replaces the vertical icon rail on desktop.
// Too many tabs for the width scroll sideways (mouse wheel or drag).
Item {
    id: root

    // ListModel rows carrying `tabId` and `icon` (an index into `icons`).
    property var model
    property var icons: []
    property var labels: ({})
    property int currentIndex: 0
    // Optional filter for contextual tabs; hidden tabs leave no gap.
    property var tabVisible: function(tabId) { return true }

    signal tabClicked(int index)

    implicitHeight: 54
    clip: true

    Accessible.role: Accessible.PageTabList

    function ensureCurrentVisible() {
        const item = repeater.itemAt(root.currentIndex)
        if (!item || !item.visible)
            return
        const maxX = Math.max(0, strip.contentWidth - strip.width)
        if (item.x < strip.contentX)
            strip.contentX = Math.max(0, item.x - 4)
        else if (item.x + item.width > strip.contentX + strip.width)
            strip.contentX = Math.min(maxX, item.x + item.width + 4 - strip.width)
    }
    onCurrentIndexChanged: Qt.callLater(ensureCurrentVisible)

    Flickable {
        id: strip
        anchors.fill: parent
        contentWidth: row.width
        contentHeight: height
        flickableDirection: Flickable.HorizontalFlick
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentWidth > width

        // A vertical wheel scrolls the strip sideways, so tabs past the edge stay reachable
        // without a horizontal wheel.
        WheelHandler {
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: function(event) {
                const delta = event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y
                const maxX = Math.max(0, strip.contentWidth - strip.width)
                strip.contentX = Math.max(0, Math.min(maxX, strip.contentX - delta / 2))
            }
        }

        Row {
            id: row
            height: strip.height
            leftPadding: 4
            rightPadding: 4

            Repeater {
                id: repeater
                model: root.model
                delegate: AbstractButton {
                    id: tab
                    required property int index
                    required property var model

                    readonly property bool current: root.currentIndex === tab.index

                    visible: root.tabVisible(tab.model.tabId)
                    width: visible ? Math.max(48, label.implicitWidth + 16) : 0
                    height: row.height
                    hoverEnabled: true

                    Accessible.role: Accessible.PageTab
                    Accessible.name: root.labels[tab.model.tabId] || ""
                    Accessible.checked: tab.current

                    onClicked: root.tabClicked(tab.index)

                    ToolTip.visible: hovered && label.truncated
                    ToolTip.text: root.labels[tab.model.tabId] || ""

                    background: Rectangle {
                        color: tab.hovered && !tab.current ? Theme.panelSecondaryBg : "transparent"
                        radius: Theme.radiusMd

                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.width - 12
                            height: 2
                            radius: 1
                            color: Theme.primary
                            visible: tab.current
                        }
                    }

                    contentItem: Item {
                        Column {
                            anchors.centerIn: parent
                            spacing: 3

                            IconGlyph {
                                anchors.horizontalCenter: parent.horizontalCenter
                                glyph: root.icons[tab.model.icon]
                                iconSize: Theme.iconSizeLg
                                iconColor: tab.current ? Theme.primary : Theme.mutedForeground
                            }

                            Text {
                                id: label
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: root.labels[tab.model.tabId] || ""
                                color: tab.current ? Theme.primary : Theme.mutedForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                font.weight: tab.current ? Font.Medium : Font.Normal
                            }
                        }
                    }
                }
            }
        }
    }

    // VEDIT: when tabs run past the edge, a fade and an arrow button on that side say so — and
    // a click scrolls there (nobody guesses that the strip scrolls).
    Repeater {
        model: [ { side: -1 }, { side: 1 } ]
        delegate: Item {
            required property var modelData
            readonly property bool more: modelData.side < 0
                                         ? strip.contentX > 1
                                         : strip.contentX < strip.contentWidth - strip.width - 1
            visible: strip.contentWidth > strip.width + 1 && more
            width: 40
            height: root.height - Theme.borderWidth
            x: modelData.side < 0 ? 0 : root.width - width
            z: 2
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: modelData.side < 0 ? Theme.panelBackground : "transparent" }
                    GradientStop { position: 0.55; color: Theme.panelBackground }
                    GradientStop { position: 1.0; color: modelData.side < 0 ? "transparent" : Theme.panelBackground }
                }
            }
            Text {
                anchors.centerIn: parent
                text: modelData.side < 0 ? "‹" : "›"
                color: Theme.panelForeground
                font.pixelSize: 26
                font.bold: true
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    const maxX = Math.max(0, strip.contentWidth - strip.width)
                    strip.contentX = Math.max(0, Math.min(maxX, strip.contentX + modelData.side * strip.width * 0.6))
                }
            }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.borderWidth
        color: Theme.panelBorder
    }
}
