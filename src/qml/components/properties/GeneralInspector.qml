import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""

    height: contentCol.height
    implicitHeight: contentCol.height

    // Human label for a clip kind. The raw id was shown to the user.
    function clipKindLabel(kind) {
        switch (kind) {
        case "video": return qsTr("Video")
        case "audio": return qsTr("Audio")
        case "image": return qsTr("Image")
        case "text": return qsTr("Text")
        case "subtitle": return qsTr("Subtitle")
        case "shape": return qsTr("Shape")
        case "sticker": return qsTr("Sticker")
        case "adjustment": return qsTr("Adjustment")
        }
        return kind.length > 0 ? kind : "—"
    }

    function applyTrim(inPoint, outPoint) {
        if (!root.hasSelection || isNaN(inPoint) || isNaN(outPoint))
            return
        EditorState.setClipTrim(EditorState.selectedTrack, EditorState.selectedClip, inPoint, outPoint)
    }

    function refreshFields() {
        if (!root.hasSelection)
            return
        if (startField && !startField.activeFocus)
            startField.value = root.clipData.start
        if (durationField && !durationField.activeFocus)
            durationField.value = root.clipData.duration
        if (inPointField && !inPointField.activeFocus)
            inPointField.value = root.clipData.inPoint
        if (outPointField && !outPointField.activeFocus)
            outPointField.value = root.clipData.outPoint
    }

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++; root.refreshFields() }
        function onSelectedClipDataChanged() { root.clipDataRevision++; root.refreshFields() }
        function onTracksChanged() { root.clipDataRevision++; root.refreshFields() }
    }

    Component.onCompleted: refreshFields()

    Column {
        id: contentCol
        width: root.width
        spacing: Theme.spacingXl

        // Read-only name plus a rename dialog: an editable field at the top of the panel kept
        // being mistaken for the text clip's content box.
        Column {
            width: root.width
            spacing: 4
            Text {
                text: qsTr("Clip name")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            Row {
                width: parent.width
                spacing: Theme.spacingSm
                Text {
                    width: parent.width - renameButton.width - parent.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    text: (root.hasSelection && root.clipData.name) || qsTr("Untitled clip")
                    color: root.hasSelection && root.clipData.name ? Theme.panelForeground : Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                IconButton {
                    id: renameButton
                    glyph: Theme.icons.pencil
                    variant: "ghost"
                    buttonSize: Theme.controlHeightSm
                    tooltip: qsTr("Rename clip")
                    onClicked: renameDialog.openWith(qsTr("Rename clip"), root.clipData.name || "")
                }
            }
        }

        Column {
            width: root.width
            spacing: 4
            Text {
                text: qsTr("Type")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            Text {
                // Human label rather than the raw internal id.
                text: root.clipKindLabel(root.clipKind)
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                elide: Text.ElideRight
                width: parent.width - x
            }
        }

        Row {
            width: parent.width
            spacing: 8

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 4
                Text {
                    text: qsTr("Starts at")
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedNumberField {
                    id: startField
                    to: 86400
                    unit: "s"
                    width: parent.width
                    decimals: 2
                    step: 0.1
                    from: 0
                    onEdited: v => EditorState.setClipStart(
                                      EditorState.selectedTrack, EditorState.selectedClip, v)
                }
            }

            Column {
                width: (parent.width - parent.spacing) / 2
                spacing: 4
                Text {
                    text: qsTr("Duration")
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                }
                ThemedNumberField {
                    id: durationField
                    to: 86400
                    unit: "s"
                    width: parent.width
                    decimals: 2
                    step: 0.1
                    from: 0.1
                    onEdited: v => EditorState.setClipDuration(
                                        EditorState.selectedTrack, EditorState.selectedClip, v)
                }
            }
        }

        // VEDIT: 드래그 없이 길이 정하기 — one press for the usual lengths of a photo, title or shape.
        Flow {
            visible: root.clipKind === "image" || root.clipKind === "text" || root.clipKind === "shape"
            width: root.width
            spacing: 6
            Repeater {
                model: [2, 3, 5, 10]
                delegate: ThemedChip {
                    required property int modelData
                    text: qsTr("%1 s").arg(modelData)
                    selected: Math.abs(Number(root.clipData.duration || 0) - modelData) < 0.05
                    onClicked: EditorState.setClipDuration(EditorState.selectedTrack, EditorState.selectedClip, modelData)
                }
            }
        }

        // VEDIT: CapCut's 사진 길이 한 번에 — for slideshows, every photo gets this one's length.
        ThemedButton {
            visible: root.clipKind === "image"
            width: root.width
            text: qsTr("Give every photo this length")
            onClicked: EditorState.setAllPhotoDurations(durationField.value)
        }

        Column {
            width: root.width
            spacing: 8
            visible: root.clipKind !== "text" && root.clipKind !== "subtitle"
                     && root.clipKind !== "adjustment" && root.clipKind !== "shape"

            Text {
                text: qsTr("Trim")
                HoverHandler { id: tipHover1217 }
                ThemedToolTip { text: qsTr("Which part of the original file this clip plays"); visible: tipHover1217.hovered }
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Row {
                width: parent.width
                spacing: 8

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("From")
                        HoverHandler { id: tipHover1231 }
                        ThemedToolTip { text: qsTr("Seconds into the file where this clip starts"); visible: tipHover1231.hovered }
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: inPointField
                        clockFormat: true // VEDIT: 분:초
                        to: 86400
                        unit: "s"
                        width: parent.width
                        decimals: 2
                        step: 0.1
                        from: 0
                        onEdited: v => root.applyTrim(v, root.clipData.outPoint)
                    }
                }

                Column {
                    width: (parent.width - parent.spacing) / 2
                    spacing: 4
                    Text {
                        text: qsTr("To")
                        HoverHandler { id: tipHover1252 }
                        ThemedToolTip { text: qsTr("Seconds into the file where this clip ends"); visible: tipHover1252.hovered }
                        color: Theme.mutedForeground
                        font.pixelSize: Theme.fontSizeXs
                        font.family: Theme.fontFamily
                    }
                    ThemedNumberField {
                        id: outPointField
                        clockFormat: true // VEDIT: 분:초
                        to: 86400
                        unit: "s"
                        width: parent.width
                        decimals: 2
                        step: 0.1
                        from: 0
                        onEdited: v => root.applyTrim(root.clipData.inPoint, v)
                    }
                }
            }
        }

        // VEDIT (조박사님): the file in detail — name, folder, size, dates, picture size, frame
        // rate, formats, sound — and a button to open its folder.
        Column {
            width: root.width
            spacing: 6
            visible: root.clipData.path !== undefined && root.clipData.path.length > 0

            readonly property var details: {
                void root.clipData.path
                return EditorState.clipFileDetails(EditorState.selectedTrack, EditorState.selectedClip)
            }

            Text {
                text: qsTr("File information")
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
            }

            Repeater {
                model: parent.details
                delegate: Row {
                    required property var modelData
                    width: root.width
                    spacing: 8
                    Text {
                        width: 96
                        text: modelData.label
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        width: parent.width - 96 - parent.spacing
                        text: modelData.value
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        wrapMode: Text.WrapAnywhere
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }
                }
            }

            ThemedButton {
                text: qsTr("Open its folder")
                onClicked: EditorState.revealClipFile(EditorState.selectedTrack, EditorState.selectedClip)
            }
        }
    }

    NameDialog {
        id: renameDialog
        acceptText: qsTr("Rename")
        placeholder: qsTr("Clip name")
        onSubmitted: name => EditorState.setClipName(EditorState.selectedTrack, EditorState.selectedClip, name)
    }
}
