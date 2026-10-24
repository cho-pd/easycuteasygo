import QtQuick
import QtQuick.Controls.Basic
import QtCore // VEDIT: Settings
import Drift

// Shown at startup in place of a fresh empty project whenever "Reopen last project
// on startup" is off: what you were working on, and how to start something. The
// mobile equivalent is AndroidProjectsPage; this mirrors its structure (new project,
// open project, recent projects) with desktop-sized controls and ProjectRow's
// styling (RecentProjectsPopup) for the recents list.
Rectangle {
    id: root

    signal newProjectRequested()
    signal openProjectRequested()
    signal openRecentRequested(string path)

    color: Theme.appBackground

    readonly property var items: EditorState.recentProjects

    // VEDIT: 바둑판 / 목록 — how the recents are shown, remembered between runs.
    Settings {
        id: startSettings
        category: "startScreen"
        property string recentView: "grid"
    }
    readonly property bool gridView: startSettings.recentView !== "list"

    // The list look: one wide row per project, small picture on the left.
    component ProjectListRow: Rectangle {
        id: listRow

        required property var modelData
        readonly property bool exists: modelData.exists !== false
        readonly property bool hovered: listHover.hovered

        width: recentColumn.width
        height: 84
        radius: Theme.radiusMd
        color: hovered && exists ? Theme.accent : Theme.panelBackground
        border.width: Theme.borderWidth
        border.color: Theme.panelBorder
        opacity: exists ? 1 : 0.6

        HoverHandler {
            id: listHover
        }

        Row {
            anchors.left: parent.left
            anchors.right: listRemove.left
            anchors.leftMargin: Theme.spacingXl
            anchors.rightMargin: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingLg

            Rectangle {
                width: 120
                height: 68
                radius: Theme.radiusSm
                anchors.verticalCenter: parent.verticalCenter
                color: "#000000"
                border.width: Theme.borderWidth
                border.color: Theme.panelBorder
                clip: true
                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    source: listRow.modelData.thumb || ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: false
                    sourceSize.width: 240
                }
                IconGlyph {
                    anchors.centerIn: parent
                    visible: !listRow.modelData.thumb
                    glyph: Theme.icons.film
                    iconSize: Theme.iconSizeXl
                    iconColor: Theme.mutedForeground
                }
            }

            Column {
                width: parent.width - 120 - Theme.spacingLg
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2

                Text {
                    width: parent.width
                    text: listRow.modelData.name + (listRow.exists ? "" : qsTr(" (missing)"))
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 17
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: {
                        const parts = []
                        if (listRow.modelData.when) parts.push(listRow.modelData.when)
                        if (listRow.modelData.length) parts.push(qsTr("Length %1").arg(listRow.modelData.length))
                        return parts.length > 0 ? parts.join(" · ") : listRow.modelData.path
                    }
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: 16
                    elide: Text.ElideRight
                }
            }
        }

        ThemedToolTip {
            text: listRow.exists
                  ? listRow.modelData.path
                  : qsTr("This file has been moved or deleted:\n%1").arg(listRow.modelData.path)
            visible: listRow.hovered && !listRemoveArea.containsMouse
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: listRow.exists ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: {
                if (listRow.exists)
                    root.openRecentRequested(listRow.modelData.path)
            }
        }

        Item {
            id: listRemove
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            width: 48
            height: 48
            opacity: listRow.hovered ? 1 : 0

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.x
                iconSize: Theme.iconSizeMd
                iconColor: listRemoveArea.containsMouse ? Theme.foreground : Theme.mutedForeground
            }
            ThemedToolTip {
                text: qsTr("Remove from recents")
                visible: listRemoveArea.containsMouse
            }
            MouseArea {
                id: listRemoveArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: (mouse) => {
                    mouse.accepted = true
                    EditorState.removeRecentProject(listRow.modelData.path)
                }
            }
        }
    }

    // VEDIT: 바둑판처럼 — each recent project is a card (opening picture on top, name, when and
    // length below), several to a row, like CapCut's drafts.
    component ProjectRow: Rectangle {
        id: projectRow

        required property var modelData
        readonly property bool exists: modelData.exists !== false
        readonly property bool hovered: rowHover.hovered

        width: root.cardWidth
        height: thumbBox.height + infoColumn.implicitHeight + Theme.spacingMd * 2 + Theme.spacingSm
        radius: Theme.radiusMd
        color: hovered && exists ? Theme.accent : Theme.panelBackground
        border.width: hovered && exists ? 2 : Theme.borderWidth
        border.color: hovered && exists ? Theme.primary : Theme.panelBorder
        opacity: exists ? 1 : 0.6

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        HoverHandler {
            id: rowHover
        }

        Rectangle {
            id: thumbBox
            x: Theme.spacingMd
            y: Theme.spacingMd
            width: parent.width - Theme.spacingMd * 2
            height: Math.round(width * 9 / 16)
            radius: Theme.radiusSm
            color: "#000000"
            border.width: Theme.borderWidth
            border.color: Theme.panelBorder
            clip: true
            Image {
                anchors.fill: parent
                anchors.margins: 1
                source: projectRow.modelData.thumb || ""
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                cache: false
                sourceSize.width: 480
            }
            IconGlyph {
                anchors.centerIn: parent
                visible: !projectRow.modelData.thumb
                glyph: Theme.icons.film
                iconSize: Theme.iconSizeXl
                iconColor: Theme.mutedForeground
            }
        }

        Column {
            id: infoColumn
            anchors.left: thumbBox.left
            anchors.right: thumbBox.right
            anchors.top: thumbBox.bottom
            anchors.topMargin: Theme.spacingSm
            spacing: 2

            Text {
                width: parent.width
                text: projectRow.modelData.name + (projectRow.exists ? "" : qsTr(" (missing)"))
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: 16
                font.weight: Font.Medium
                elide: Text.ElideRight
            }

            // when it was last worked on and how long it is; the path is in the tooltip.
            Text {
                width: parent.width
                text: {
                    const parts = []
                    if (projectRow.modelData.when) parts.push(projectRow.modelData.when)
                    if (projectRow.modelData.length) parts.push(qsTr("Length %1").arg(projectRow.modelData.length))
                    return parts.length > 0 ? parts.join(" · ") : projectRow.modelData.path
                }
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: 16
                elide: Text.ElideRight
            }
        }

        ThemedToolTip {
            text: projectRow.exists
                  ? projectRow.modelData.path
                  : qsTr("This file has been moved or deleted:\n%1").arg(projectRow.modelData.path)
            visible: projectRow.hovered && !removeArea.containsMouse
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: projectRow.exists ? Qt.PointingHandCursor : Qt.ArrowCursor
            onClicked: {
                if (!projectRow.exists)
                    return
                root.openRecentRequested(projectRow.modelData.path)
            }
        }

        // Remove-from-recents: a round button on the picture's corner, shown on hover.
        Rectangle {
            id: removeButton
            anchors.right: thumbBox.right
            anchors.top: thumbBox.top
            anchors.margins: Theme.spacingSm
            width: 30
            height: 30
            radius: 15
            color: Qt.rgba(0, 0, 0, 0.65)
            opacity: projectRow.hovered ? 1 : 0
            visible: opacity > 0

            Behavior on opacity {
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.x
                iconSize: Theme.iconSizeMd
                iconColor: removeArea.containsMouse ? "#ffffff" : "#d0d0d0"
            }

            ThemedToolTip {
                text: qsTr("Remove from recents")
                visible: removeArea.containsMouse
            }

            MouseArea {
                id: removeArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: (mouse) => {
                    mouse.accepted = true
                    EditorState.removeRecentProject(projectRow.modelData.path)
                }
            }
        }
    }

    // VEDIT: as many cards per row as fit (at least 2, about 130 px each — half the first size,
    // 조박사님), filling the row.
    readonly property real gridGap: Theme.spacingMd
    readonly property int cardColumns: Math.max(2, Math.floor((column.width + gridGap) / (130 + gridGap)))
    readonly property real cardWidth: Math.floor((column.width - gridGap * (cardColumns - 1)) / cardColumns)

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, column.implicitHeight + Theme.spacing3xl * 2)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        Column {
            id: column
            width: Math.min(1180, flick.width - Theme.spacing3xl * 2) // VEDIT: room for a grid
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(Theme.spacing3xl, (flick.height - implicitHeight) / 2)
            spacing: Theme.spacing3xl

            Column {
                width: parent.width
                spacing: Theme.spacingMd // VEDIT: room between the logo and the slogan

                // VEDIT: the brand logo (white, for the black stage) — 조박사님's design system, img/.
                // Clear space comes from the column spacing around it.
                Image {
                    source: "qrc:/app/brand/logo-white.png"
                    height: 44
                    width: Math.round(height * sourceSize.width / Math.max(1, sourceSize.height))
                    fillMode: Image.PreserveAspectFit
                    smooth: true
                    mipmap: true
                    Accessible.name: "Easy Cut, Easy Go!"
                }

                Text {
                    text: "쉽게 자르고, 쉽게 간다" // VEDIT: Korean slogan (조박사님)
                    color: Theme.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: 18
                }
            }

            Row {
                width: parent.width
                spacing: Theme.spacingLg

                ThemedButton {
                    variant: "primary"
                    glyph: Theme.icons.plus
                    text: qsTr("New Project")
                    onClicked: root.newProjectRequested()
                }

                ThemedButton {
                    variant: "secondary"
                    glyph: Theme.icons.folder
                    text: qsTr("Open Project…")
                    onClicked: root.openProjectRequested()
                }
            }

            Column {
                id: recentColumn
                width: parent.width
                spacing: Theme.spacingMd

                // VEDIT: the heading with the 바둑판 / 목록 switch beside it.
                Row {
                    spacing: Theme.spacingLg
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Recent Projects")
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: 16
                        font.weight: Font.Medium
                    }
                    Row {
                        spacing: Theme.spacingSm
                        visible: root.items.length > 0
                        ThemedToggleButton { // VEDIT: icons, the words in the tooltip
                            width: 24 // 조박사님: half size (an exception to the 48 px rule)
                            toggleHeight: 24
                            glyph: Theme.icons.layoutGrid
                            glyphSize: 14
                            tooltip: qsTr("Grid")
                            checked: root.gridView
                            onClicked: startSettings.recentView = "grid"
                        }
                        ThemedToggleButton {
                            width: 24 // 조박사님: half size (an exception to the 48 px rule)
                            toggleHeight: 24
                            glyph: Theme.icons.list
                            glyphSize: 14
                            tooltip: qsTr("List")
                            checked: !root.gridView
                            onClicked: startSettings.recentView = "list"
                        }
                    }
                }

                ThemedLabel {
                    width: parent.width
                    visible: root.items.length === 0
                    wrapMode: Text.WordWrap
                    text: qsTr("Nothing here yet — projects you save will show up in this list.")
                }

                // VEDIT: a grid of cards instead of a list (the outer Flickable scrolls the page).
                Flow {
                    id: recentList
                    width: parent.width
                    spacing: root.gridGap
                    visible: root.items.length > 0 && root.gridView
                    Repeater {
                        model: root.gridView ? root.items : []
                        delegate: ProjectRow { }
                    }
                }

                Column {
                    width: parent.width
                    spacing: Theme.spacingSm
                    visible: root.items.length > 0 && !root.gridView
                    Repeater {
                        model: root.gridView ? [] : root.items
                        delegate: ProjectListRow { }
                    }
                }
            }
        }
    }
}
