// VEDIT: 글자 › 기본 — laid out like CapCut's basic text page (docs/capcut-text-panel-ref.md):
// a name on the left of each row and its control on the right, small type, compact controls,
// then the 사전 설정 스타일 grid, and folding sections (변형 · 혼합 · 획 · 배경 · 글로우 · 그림자 · 곡선)
// each with a tick to switch it on. Each change is one undo step. (조박사님 2026-10-21: "글자도 너무
// 크고 촌스러워, 캡컷과 같은 크기와 정렬" — this replaces the earlier 16 px / 48 px version.)
import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

Column {
    id: root
    spacing: 10

    property int rev: 0
    readonly property var b: {
        void rev
        void EditorState.playheadSeconds
        return EditorState.textBasics(EditorState.selectedTrack, EditorState.selectedClip)
    }
    readonly property bool isText: b.kind === "text"
    readonly property real labelWidth: 84
    readonly property real fieldHeight: 26
    readonly property real fieldWidth: 64

    function set(patch) {
        EditorState.setTextBasics(EditorState.selectedTrack, EditorState.selectedClip, patch)
    }
    function one(key, value) {
        const p = {}
        p[key] = value
        set(p)
    }

    Connections {
        target: EditorState
        function onSelectionChanged() { root.rev++ }
        function onSelectedClipDataChanged() { root.rev++ }
        function onTracksChanged() { root.rev++ }
    }

    component Name: Text {
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
        elide: Text.ElideRight
    }

    // One form row: the name in a fixed left column, the control after it.
    component FormRow: Item {
        id: fr
        property string label: ""
        default property alias content: frBody.data
        width: parent ? parent.width : 200
        height: Math.max(root.fieldHeight, frBody.childrenRect.height)
        Name {
            width: root.labelWidth - 6
            anchors.verticalCenter: parent.verticalCenter
            text: fr.label
        }
        Item {
            id: frBody
            x: root.labelWidth
            width: parent.width - root.labelWidth
            height: parent.height
        }
    }

    // CapCut's slider line: the name above, the slider and a number box (with steppers) beside.
    component SliderRow: Column {
        id: sr
        property string label: ""
        // VEDIT (조박사님, CapCut): the text panel's own ‹ ◆ ›. `keyKey` is this value's name in
        // textBasics; the panel finds the keyframe property behind it (none: no ◆).
        property string keyKey: ""
        readonly property string keyProp: {
            void root.rev
            return keyKey.length > 0
                   ? EditorState.textBasicsKeyProp(EditorState.selectedTrack, EditorState.selectedClip, keyKey) : ""
        }
        readonly property var keyState: {
            void root.rev
            void EditorState.playheadSeconds
            void EditorState.tracksRevision
            return keyProp.length > 0
                   ? EditorState.clipPropsKeyState(EditorState.selectedTrack, EditorState.selectedClip, [keyProp]) : ({})
        }
        readonly property bool keyed: keyState.animated === true
        // Keyed, the number is the one at the playhead.
        readonly property real shownValue: {
            void EditorState.playheadSeconds
            void root.rev
            return keyed ? EditorState.propertyValueAt(EditorState.selectedTrack, EditorState.selectedClip,
                                                       keyProp, EditorState.playheadSeconds, sr.value)
                         : sr.value
        }
        function commit(v) {
            if (keyed)
                EditorState.setClipKeyframe(EditorState.selectedTrack, EditorState.selectedClip, keyProp,
                                            EditorState.playheadSeconds, v)
            else
                sr.committed(v)
        }
        property real from: 0
        property real to: 100
        property real value: 0
        property int decimals: 0
        property string unit: ""
        property real factor: 1 // shown number = value × factor (opacity 0..1 shows 0..100 %)
        signal committed(real v)
        width: parent ? parent.width : 200
        spacing: 2
        Item {
            width: parent.width
            height: Math.max(srName.implicitHeight, srKey.visible ? srKey.height : 0)
            visible: sr.label.length > 0 || srKey.visible
            Name {
                id: srName
                text: sr.label
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
            }
            KeyframeToggle {
                id: srKey
                visible: sr.keyProp.length > 0
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                animated: sr.keyed
                atPlayhead: sr.keyState.atPlayhead === true
                prevSeconds: sr.keyState.prev !== undefined ? sr.keyState.prev : -1
                nextSeconds: sr.keyState.next !== undefined ? sr.keyState.next : -1
                onToggled: EditorState.toggleClipKeyframeHere(EditorState.selectedTrack, EditorState.selectedClip,
                                                              sr.keyProp, sr.shownValue)
            }
        }
        Item {
            width: parent.width
            height: root.fieldHeight
            ThemedSlider {
                anchors.left: parent.left
                anchors.right: srField.left
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                from: sr.from
                to: sr.to
                value: sr.shownValue
                showValueTooltip: false
                onPressedChanged: {
                    if (!pressed)
                        sr.commit(value)
                }
            }
            ThemedNumberField {
                id: srField
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: root.fieldWidth + 14
                height: root.fieldHeight
                font.pixelSize: Theme.fontSizeSm
                from: sr.from * sr.factor
                to: sr.to * sr.factor
                decimals: sr.decimals
                unit: sr.unit
                value: sr.shownValue * sr.factor
                onEdited: v => sr.commit(v / sr.factor)
            }
        }
    }

    // A folding section: a tick switches the effect on, the arrow opens its settings.
    component Section: Column {
        id: sec
        property string title: ""
        property bool checkable: true
        property bool on: true
        property bool open: false
        signal toggled(bool on)
        default property alias content: secBody.data
        width: parent ? parent.width : 200
        spacing: 8
        Rectangle { width: parent.width; height: 1; color: Theme.panelBorder }
        Item {
            width: parent.width
            height: 26
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                ThemedCheckBox {
                    visible: sec.checkable
                    anchors.verticalCenter: parent.verticalCenter
                    checked: sec.on
                    onToggled: {
                        sec.toggled(checked)
                        if (checked)
                            sec.open = true
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: sec.title
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.Medium
                }
                IconGlyph {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: sec.open ? Theme.icons.chevronUp : Theme.icons.chevronDown
                    iconSize: 12
                    iconColor: Theme.mutedForeground
                }
            }
            MouseArea {
                anchors.fill: parent
                anchors.leftMargin: sec.checkable ? 26 : 0
                cursorShape: Qt.PointingHandCursor
                onClicked: sec.open = !sec.open
            }
        }
        Column {
            id: secBody
            visible: sec.open && (sec.on || !sec.checkable)
            width: parent.width
            spacing: 8
        }
    }

    component ColourField: Row {
        id: cf
        property string hex: "#ffffffff"
        signal picked(string hex)
        spacing: 6
        ColorSwatchField {
            width: 70
            height: root.fieldHeight
            hex: cf.hex
            tooltip: qsTr("Choose any colour")
            onEdited: value => cf.picked(value)
        }
        // A few ready colours, one press each.
        Repeater {
            model: ["#ffffffff", "#ff111111", "#ffffd640", "#ffe62828", "#ff4fc3f7", "#ff2ecc40"]
            delegate: Rectangle {
                required property string modelData
                anchors.verticalCenter: parent.verticalCenter
                width: 18
                height: 18
                radius: 9
                color: modelData
                border.width: String(cf.hex).toLowerCase() === modelData ? 2 : 1
                border.color: String(cf.hex).toLowerCase() === modelData ? Theme.focusRing : Theme.panelBorder
                MouseArea {
                    anchors.fill: parent
                    anchors.margins: -3
                    cursorShape: Qt.PointingHandCursor
                    onClicked: cf.picked(parent.modelData)
                }
            }
        }
    }

    component SmallToggle: ThemedToggleButton {
        toggleHeight: root.fieldHeight
        labelSize: Theme.fontSizeSm
    }

    // --- 글꼴 · 크기 -------------------------------------------------------------------------------
    FormRow {
        label: qsTr("Font")
        FontPicker {
            width: parent.width
            family: root.b.fontFamily || ""
            onFamilyPicked: family => root.one("fontFamily", family)
        }
    }

    SliderRow {
        label: qsTr("Font size")
        from: 8
        to: 300
        value: Number(root.b.pixelSize || 64)
        onCommitted: v => root.one("pixelSize", Math.round(v))
    }

    // --- 패턴 · 대소문자 ------------------------------------------------------------------------------
    FormRow {
        label: qsTr("Pattern")
        Row {
            spacing: 6
            Repeater {
                model: [{ key: "bold", glyph: "B", label: qsTr("Bold") },
                        { key: "underline", glyph: "U", label: qsTr("Underline") },
                        { key: "italic", glyph: "I", label: qsTr("Italic") }]
                delegate: SmallToggle {
                    required property var modelData
                    width: 28
                    text: modelData.glyph
                    labelBold: modelData.key === "bold"
                    labelUnderline: modelData.key === "underline"
                    labelItalic: modelData.key === "italic"
                    tooltip: modelData.label
                    checked: root.b[modelData.key] === true
                    onClicked: root.one(modelData.key, !(root.b[modelData.key] === true))
                }
            }
        }
    }

    FormRow {
        visible: root.isText
        label: qsTr("Letter case")
        Row {
            spacing: 2
            Repeater {
                model: [{ id: "upper", glyph: "TT", label: qsTr("All capitals") },
                        { id: "lower", glyph: "tt", label: qsTr("All small letters") },
                        { id: "title", glyph: "Tt", label: qsTr("Capital first letters") }]
                delegate: SmallToggle {
                    required property var modelData
                    width: 36
                    text: modelData.glyph
                    labelBold: true
                    tooltip: modelData.label
                    onClicked: root.one("textCase", modelData.id)
                }
            }
        }
    }

    FormRow {
        label: qsTr("Colour")
        ColourField {
            hex: root.b.color || "#ffffffff"
            onPicked: hex => root.one("color", hex)
        }
    }

    // --- 간격 · 정렬 ------------------------------------------------------------------------------
    FormRow {
        label: qsTr("Letter spacing")
        Row {
            spacing: 10
            ThemedNumberField {
                width: root.fieldWidth
                height: root.fieldHeight
                font.pixelSize: Theme.fontSizeSm
                from: -10
                to: 60
                value: Number(root.b.letterSpacing || 0)
                onEdited: v => root.one("letterSpacing", Math.round(v))
            }
            Name { text: qsTr("Line spacing"); anchors.verticalCenter: parent.verticalCenter }
            ThemedNumberField {
                width: root.fieldWidth
                height: root.fieldHeight
                font.pixelSize: Theme.fontSizeSm
                from: 0.6
                to: 3
                decimals: 1
                step: 0.1
                value: Number(root.b.lineHeight || 1.2)
                onEdited: v => root.one("lineHeight", v)
            }
        }
    }

    FormRow {
        label: qsTr("Alignment")
        Rectangle {
            width: alignRow.width + 8
            height: root.fieldHeight
            radius: Theme.radiusSm
            color: Theme.panelAccent
            Row {
                id: alignRow
                anchors.centerIn: parent
                spacing: 2
                Repeater {
                    model: [{ key: "align", value: "left", glyph: Theme.icons.alignLeft, label: qsTr("Align left") },
                            { key: "align", value: "center", glyph: Theme.icons.alignCenter, label: qsTr("Align centre") },
                            { key: "align", value: "right", glyph: Theme.icons.alignRight, label: qsTr("Align right") },
                            { key: "valign", value: "top", glyph: Theme.icons.alignTop, label: qsTr("Align top") },
                            { key: "valign", value: "middle", glyph: Theme.icons.alignMiddle, label: qsTr("Align middle") },
                            { key: "valign", value: "bottom", glyph: Theme.icons.alignBottom, label: qsTr("Align bottom") }]
                    delegate: ThemedToggleButton {
                        required property var modelData
                        width: 28
                        toggleHeight: root.fieldHeight - 4
                        glyph: modelData.glyph
                        tooltip: modelData.label
                        checked: root.b[modelData.key] === (modelData.value === "middle" ? "center" : modelData.value)
                        onClicked: root.one(modelData.key, modelData.value)
                    }
                }
            }
        }
    }

    // --- 사전 설정 스타일 ---------------------------------------------------------------------------
    Rectangle { width: parent.width; height: 1; color: Theme.panelBorder }
    Name { text: qsTr("Preset styles") }
    Flow {
        width: parent.width
        spacing: 6
        Repeater {
            model: EditorState.textLookPresets()
            delegate: Rectangle {
                required property var modelData
                width: 40
                height: 40
                radius: 6
                color: modelData.bg !== "" ? modelData.bg : "#ff2c2c2e"
                border.width: lookArea.containsMouse ? 2 : 0
                border.color: Theme.focusRing
                Text {
                    anchors.centerIn: parent
                    text: modelData.id === "plain" ? "⊘" : "Aa"
                    color: modelData.fill
                    font.family: Theme.fontFamily
                    font.pixelSize: 15
                    font.bold: true
                    style: modelData.stroke !== "" ? Text.Outline : (modelData.glow !== "" ? Text.Raised : Text.Normal)
                    styleColor: modelData.stroke !== "" ? modelData.stroke : (modelData.glow !== "" ? modelData.glow : "transparent")
                }
                MouseArea {
                    id: lookArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: EditorState.applyReadyTextLook(EditorState.selectedTrack, EditorState.selectedClip,
                                                         parent.modelData.id)
                    ToolTip.visible: containsMouse
                    ToolTip.delay: 400
                    ToolTip.text: parent.modelData.label
                }
            }
        }
    }

    // --- 변형 --------------------------------------------------------------------------------------
    Section {
        visible: root.isText
        title: qsTr("Transform")
        checkable: false
        open: true
        Name {
            visible: root.b.moving === true
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("This text moves over time, so its place is set with keyframes on the Transform tab.")
        }
        FormRow {
            visible: root.b.moving !== true
            label: qsTr("Position")
            Row {
                spacing: 6
                Name { text: "X"; anchors.verticalCenter: parent.verticalCenter }
                ThemedNumberField {
                    width: 78
                    height: root.fieldHeight
                    font.pixelSize: Theme.fontSizeSm
                    from: -4000
                    to: 4000
                    value: Number(root.b.posX || 0)
                    onEdited: v => root.one("posX", v)
                }
                Name { text: "Y"; anchors.verticalCenter: parent.verticalCenter }
                ThemedNumberField {
                    width: 78
                    height: root.fieldHeight
                    font.pixelSize: Theme.fontSizeSm
                    from: -4000
                    to: 4000
                    value: Number(root.b.posY || 0)
                    onEdited: v => root.one("posY", v)
                }
            }
        }
        FormRow {
            visible: root.b.moving !== true
            label: qsTr("Rotation")
            ThemedNumberField {
                width: 78
                height: root.fieldHeight
                font.pixelSize: Theme.fontSizeSm
                from: -180
                to: 180
                unit: "°"
                value: Number(root.b.rotation || 0)
                onEdited: v => root.one("rotation", Math.round(v))
            }
        }
        Rectangle {
            visible: root.b.moving !== true
            width: placeRow.width + 8
            height: root.fieldHeight
            radius: Theme.radiusSm
            color: Theme.panelAccent
            Row {
                id: placeRow
                anchors.centerIn: parent
                spacing: 2
                Repeater {
                    model: [{ id: "left", glyph: Theme.icons.alignLeft, label: qsTr("Left") },
                            { id: "hcenter", glyph: Theme.icons.alignCenter, label: qsTr("Centre") },
                            { id: "right", glyph: Theme.icons.alignRight, label: qsTr("Right") },
                            { id: "top", glyph: Theme.icons.alignTop, label: qsTr("Top") },
                            { id: "vcenter", glyph: Theme.icons.alignMiddle, label: qsTr("Middle") },
                            { id: "bottom", glyph: Theme.icons.alignBottom, label: qsTr("Bottom") }]
                    delegate: ThemedToggleButton {
                        required property var modelData
                        width: 28
                        toggleHeight: root.fieldHeight - 4
                        glyph: modelData.glyph
                        tooltip: modelData.label
                        onClicked: EditorState.alignTextOnCanvas(EditorState.selectedTrack, EditorState.selectedClip,
                                                                 modelData.id)
                    }
                }
            }
        }
    }

    // --- 혼합 · 획 · 배경 · 글로우 · 그림자 · 곡선 -----------------------------------------------------
    Section {
        title: qsTr("Blend")
        checkable: false
        open: true
        SliderRow {
            keyKey: "opacity" // VEDIT
            label: qsTr("Opacity")
            from: 0
            to: 1
            factor: 100
            unit: "%"
            value: Number(root.b.opacity !== undefined ? root.b.opacity : 1)
            onCommitted: v => root.one("opacity", v)
        }
    }

    Section {
        title: qsTr("Stroke")
        on: root.b.strokeOn === true
        onToggled: on => root.one("strokeOn", on)
        FormRow {
            label: qsTr("Colour")
            ColourField {
                hex: root.b.strokeColor || "#ff000000"
                onPicked: hex => root.one("strokeColor", hex)
            }
        }
        SliderRow {
            keyKey: "strokeWidth" // VEDIT
            label: qsTr("Thickness")
            from: 1
            to: 30
            value: Number(root.b.strokeWidth || 6)
            unit: "px"
            onCommitted: v => root.one("strokeWidth", Math.round(v))
        }
    }

    Section {
        title: qsTr("Background")
        on: root.b.bgOn === true
        onToggled: on => root.one("bgOn", on)
        FormRow {
            label: qsTr("Colour")
            ColourField {
                hex: "#ff" + String(root.b.bgColor || "#000000").slice(1)
                onPicked: hex => root.one("bgColor", "#" + hex.slice(3))
            }
        }
        SliderRow {
            label: qsTr("Opacity")
            from: 0
            to: 1
            factor: 100
            unit: "%"
            value: Number(root.b.bgOpacity !== undefined ? root.b.bgOpacity : 0.5)
            onCommitted: v => root.one("bgOpacity", v)
        }
        SliderRow {
            label: qsTr("Round corners")
            from: 0
            to: 60
            value: Number(root.b.bgRadius || 0)
            onCommitted: v => root.one("bgRadius", Math.round(v))
        }
        // VEDIT (조박사님, CapCut): the sides and the top and bottom apart.
        SliderRow {
            keyKey: "bgPadding" // VEDIT
            label: qsTr("Side padding")
            from: 0
            to: 120
            value: Number(root.b.bgPadding || 8)
            onCommitted: v => root.one("bgPadding", Math.round(v))
        }
        SliderRow {
            keyKey: "bgPaddingY" // VEDIT
            label: qsTr("Top and bottom padding")
            from: 0
            to: 120
            value: Number(root.b.bgPaddingY !== undefined ? root.b.bgPaddingY : (root.b.bgPadding || 8))
            onCommitted: v => root.one("bgPaddingY", Math.round(v))
        }
    }

    Section {
        title: qsTr("Glow")
        on: root.b.glowOn === true
        onToggled: on => root.one("glowOn", on)
        FormRow {
            label: qsTr("Colour")
            ColourField {
                hex: root.b.glowColor || "#ffffd640"
                onPicked: hex => root.one("glowColor", hex)
            }
        }
        SliderRow {
            keyKey: "glowRadius" // VEDIT
            label: qsTr("Size")
            from: 0
            to: 60
            value: Number(root.b.glowRadius || 18)
            onCommitted: v => root.one("glowRadius", Math.round(v))
        }
        SliderRow {
            keyKey: "glowOpacity" // VEDIT
            label: qsTr("Strength")
            from: 0
            to: 1
            factor: 100
            unit: "%"
            value: Number(root.b.glowOpacity !== undefined ? root.b.glowOpacity : 0.9)
            onCommitted: v => root.one("glowOpacity", v)
        }
    }

    Section {
        title: qsTr("Shadow")
        on: root.b.shadowOn === true
        onToggled: on => root.one("shadowOn", on)
        FormRow {
            label: qsTr("Colour")
            ColourField {
                hex: root.b.shadowColor || "#ff000000"
                onPicked: hex => root.one("shadowColor", hex)
            }
        }
        SliderRow {
            keyKey: "shadowOpacity" // VEDIT
            label: qsTr("Opacity")
            from: 0
            to: 1
            factor: 100
            unit: "%"
            value: Number(root.b.shadowOpacity !== undefined ? root.b.shadowOpacity : 0.6)
            onCommitted: v => root.one("shadowOpacity", v)
        }
        SliderRow {
            keyKey: "shadowBlur" // VEDIT
            label: qsTr("Blur")
            from: 0
            to: 40
            value: Number(root.b.shadowBlur || 10)
            onCommitted: v => root.one("shadowBlur", Math.round(v))
        }
        SliderRow {
            label: qsTr("Distance")
            from: 0
            to: 60
            value: Number(root.b.shadowDistance || 8)
            onCommitted: v => root.one("shadowDistance", Math.round(v))
        }
        SliderRow {
            label: qsTr("Angle")
            from: -180
            to: 180
            unit: "°"
            value: Number(root.b.shadowAngle !== undefined ? root.b.shadowAngle : -45)
            onCommitted: v => root.one("shadowAngle", Math.round(v))
        }
    }

    Section {
        title: qsTr("Curve")
        on: Math.abs(Number(root.b.curve || 0)) > 0.5
        onToggled: on => root.one("curve", on ? 40 : 0)
        SliderRow {
            keyKey: "curve" // VEDIT
            label: qsTr("Bend (one line only)")
            from: -100
            to: 100
            value: Number(root.b.curve || 0)
            onCommitted: v => root.one("curve", Math.round(v))
        }
    }
}
