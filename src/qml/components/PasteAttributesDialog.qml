import QtQuick
import QtQuick.Controls.Basic
import Drift

// VEDIT: 특성 붙여넣기 — laid out like CapCut's: the copied clip's attributes, grouped (effect
// stacks only when it has some, so pasting never wipes a target's effects by surprise),
// each group with its own check that turns its items on or off, all on to begin with, and a
// "길이 맞추기" check at the bottom (CapCut's keyframe timing) that stretches the copied
// keyframes and animation progress to the target clip's length. Fades always keep their seconds.
ThemedDialog {
    id: root

    title: qsTr("Paste Attributes")
    preferredWidth: Theme.dialogWidthMd
    acceptText: qsTr("Paste")
    rejectText: qsTr("Cancel")

    property var clipAttrs: ({})
    // key -> checked. Reassigned whole so bindings re-evaluate.
    property var picked: ({})
    property bool fitLength: true

    readonly property var groups: [
        { title: qsTr("Video"), items: [
            { key: "transform", label: qsTr("Position & size (motion, rotation, opacity, mask)"), avail: "isVisual" },
            { key: "animation", label: qsTr("Animation (in, out, combo)"), avail: "isVisual" },
            { key: "effects", label: qsTr("Effects & adjustments"), avail: "hasEffects", count: "effectCount" }
        ] },
        { title: qsTr("Sound"), items: [
            { key: "volume", label: qsTr("Volume"), avail: "isAudioOrVideo" },
            { key: "audioEffects", label: qsTr("Sound effects"), avail: "hasAudioEffects", count: "audioEffectCount" },
            { key: "channels", label: qsTr("Sound channels"), avail: "isAudioOrVideo" }
        ] },
        { title: qsTr("Basic"), items: [
            { key: "fades", label: qsTr("Fade in / fade out"), avail: "hasClip" },
            { key: "level", label: qsTr("Level"), avail: "hasClip" },
            { key: "speed", label: qsTr("Speed"), avail: "isAudioOrVideo" },
            { key: "transitions", label: qsTr("Transitions"), avail: "hasTransitions" }
        ] }
    ]

    function itemShown(item) { return clipAttrs && clipAttrs[item.avail] === true }
    function groupShown(group) {
        for (let i = 0; i < group.items.length; ++i)
            if (itemShown(group.items[i])) return true
        return false
    }
    function groupChecked(group) {
        let any = false
        for (let i = 0; i < group.items.length; ++i) {
            if (!itemShown(group.items[i])) continue
            if (!picked[group.items[i].key]) return false
            any = true
        }
        return any
    }
    function setPicked(key, on) {
        const next = Object.assign({}, picked)
        next[key] = on
        picked = next
    }
    function setGroup(group, on) {
        const next = Object.assign({}, picked)
        for (let i = 0; i < group.items.length; ++i)
            if (itemShown(group.items[i])) next[group.items[i].key] = on
        picked = next
    }
    readonly property bool hasAnySelected: {
        for (let g = 0; g < groups.length; ++g)
            for (let i = 0; i < groups[g].items.length; ++i)
                if (itemShown(groups[g].items[i]) && picked[groups[g].items[i].key]) return true
        return false
    }

    showAccept: hasAnySelected

    function openDialog() {
        clipAttrs = EditorState.clipboardAttributes()
        if (!clipAttrs || !clipAttrs.hasClip)
            return
        const all = {}
        for (let g = 0; g < groups.length; ++g)
            for (let i = 0; i < groups[g].items.length; ++i)
                all[groups[g].items[i].key] = itemShown(groups[g].items[i])
        picked = all
        fitLength = true
        open()
    }

    onAccepted: {
        const options = { "fitLength": root.fitLength, "replaceEffects": true }
        for (let g = 0; g < groups.length; ++g)
            for (let i = 0; i < groups[g].items.length; ++i) {
                const item = groups[g].items[i]
                options[item.key] = itemShown(item) && picked[item.key] === true
            }
        EditorState.pasteAttributes(options)
    }

    contentItem: Column {
        width: parent ? parent.width : Theme.dialogWidthMd
        spacing: Theme.spacingLg

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: 16
            text: {
                const name = (root.clipAttrs && root.clipAttrs.clipName) ? root.clipAttrs.clipName : qsTr("Clip")
                const targets = (root.clipAttrs && root.clipAttrs.targetClipCount) ? root.clipAttrs.targetClipCount : 1
                return qsTr("Pasting from “%1” onto %n selected clip(s):", "", targets).arg(name)
            }
        }

        Repeater {
            model: root.groups
            delegate: Column {
                id: groupColumn
                required property var modelData
                width: parent.width
                spacing: Theme.spacingSm
                visible: root.groupShown(modelData)

                ThemedCheckBox {
                    text: groupColumn.modelData.title
                    font.weight: Font.DemiBold
                    checked: root.groupChecked(groupColumn.modelData)
                    onToggled: root.setGroup(groupColumn.modelData, checked)
                }
                Flow {
                    width: parent.width
                    leftPadding: Theme.spacingXl
                    spacing: Theme.spacingLg
                    Repeater {
                        model: groupColumn.modelData.items
                        delegate: ThemedCheckBox {
                            required property var modelData
                            visible: root.itemShown(modelData)
                            text: {
                                const n = modelData.count ? (root.clipAttrs[modelData.count] || 0) : -1
                                return n > 0 ? modelData.label + " (" + n + ")" : modelData.label
                            }
                            checked: root.picked[modelData.key] === true
                            onToggled: root.setPicked(modelData.key, checked)
                        }
                    }
                }
                Rectangle {
                    width: parent.width
                    height: Theme.borderWidth
                    color: Theme.panelBorder
                }
            }
        }

        Column {
            width: parent.width
            spacing: 2
            ThemedCheckBox {
                width: parent.width
                text: qsTr("Fit to length")
                checked: root.fitLength
                onToggled: root.fitLength = checked
            }
            Text {
                width: parent.width
                leftPadding: Theme.spacingXl * 2
                wrapMode: Text.WordWrap
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: 16
                text: root.fitLength
                      ? qsTr("Keyframes and animation stretch to the target clip's length. Fades keep their seconds.")
                      : qsTr("Keyframes and animation keep their original times. Fades keep their seconds.")
            }
        }
    }
}
