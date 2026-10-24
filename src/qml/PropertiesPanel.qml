import QtQuick
import QtQuick.Controls.Basic
import Drift
import "components"
import "components/properties"

PanelFrame {
    id: root

    // Android bottom sheet: keep the inspector tab rail but drop the panel border
    // chrome that fights the sheet frame.
    property bool sheetMode: false
    border.width: sheetMode ? 0 : 1
    radius: sheetMode ? 0 : Theme.radiusSm
    color: sheetMode ? "transparent" : Theme.panelBackground

    // Raised by the Effects / Audio empty states; Main wires them to the
    // assets panel so the browse CTAs actually take the user somewhere.
    signal browseEffectsRequested()

    // -1 while the prompt is saving the whole stack; otherwise the one effect it was raised on.
    property int savePresetEffectIndex: -1
    signal browseAudioEffectsRequested()

    // selectedClipData is a QVariantMap; key the binding on an explicit revision
    // so nested fields such as effects refresh after project edits.
    property int clipDataRevision: 0
    property int previousTab: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!root.clipData && Object.keys(root.clipData).length > 0
    readonly property var transition: EditorState.selectedTransitionData
    readonly property bool hasTransitionSelection: !!transition && Object.keys(transition).length > 0
    readonly property int transitionTabIndex: tabIndexOf("transition")
    property string lastKindGroup: "" // VEDIT: "sound" / "picture" — the kind last selected
    readonly property string clipKind: hasSelection ? (root.clipData.kind || "") : ""
    readonly property bool hasTextStyle: hasSelection
                                         && (clipKind === "text" || clipKind === "subtitle")
                                         && !!root.clipData.textStyle

    // Inspector view state that outlives the tab being unloaded.
    QtObject {
        id: inspectorUi
        property bool sizeLinked: true
        property string textTab: "text"
        property string animSlot: "in"
        property string animCategory: ""
        property var textExpandedLayerIds: ({})
        property real contentFieldHeight: 96
        property var shapeExpandedLayerIds: ({})
    }

    property int activeTab: generalTabIndex
    readonly property int generalTabIndex: tabIndexOf("general") // VEDIT: no longer the first tab
    readonly property string currentTabId: tabsModel.get(activeTab).tabId

    onActiveTabChanged: {
        if (tabsModel.get(root.previousTab).tabId === "transition")
            transitionInspector.commitEdits()
        root.previousTab = activeTab
        transitionInspector.refreshFields()
    }

    // Kept for SubtitleEditor, which formats cue times through it.
    function formatSeconds(value) {
        return Number(value || 0).toFixed(2)
    }

    Connections {
        target: EditorState
        function onSelectionChanged() {
            root.clipDataRevision++
            root.syncSubtitlesTab()
            root.syncShapeTab()
            root.syncTextTab()
            root.syncAnimationTab()
            root.syncStabilizeTab()
            root.syncAdjustmentTab()
            root.syncActiveTab()
            // VEDIT (조박사님): only when the kind of clip changes — picture to sound opens 소리,
            // sound to picture opens 변형; picture to picture (or a click on the same clip) keeps
            // whatever tab is open.
            const kindGroup = root.clipKind === "audio" ? "sound"
                            : (root.clipKind === "video" || root.clipKind === "image") ? "picture" : ""
            if (kindGroup !== "" && kindGroup !== root.lastKindGroup) {
                if (kindGroup === "sound" && root.tabIndexOf("audio") >= 0)
                    root.activeTab = root.tabIndexOf("audio")
                else if (kindGroup === "picture"
                         && root.tabIndexOf("transform") >= 0 && root.tabVisible("transform"))
                    root.activeTab = root.tabIndexOf("transform") // from sound, nothing, or another kind
            }
            // Nothing picked (or a title, a shape…) counts as a change too, so the next picture
            // clip opens on 변형 again.
            root.lastKindGroup = kindGroup
        }
        function onSelectedClipDataChanged() {
            root.clipDataRevision++
            root.syncSubtitlesTab()
        }
        function onSelectedTransitionDataChanged() {
            if (root.hasTransitionSelection)
                root.activeTab = root.transitionTabIndex
        }
        function onTracksChanged() {
            root.clipDataRevision++
        }
        // A text clip added with no text lands in its content field. addTextClip has already
        // selected it, so the Text tab exists; callLater lets the tab switch settle before the
        // focus lands on a child of the page that was hidden a moment ago.
        function onInlineTextEditRequested(trackIndex, clipIndex) {
            if (root.textTabIndex < 0 || !root.tabVisible("text"))
                return
            root.activeTab = root.textTabIndex
            Qt.callLater(() => {
                if (textLoader.item)
                    textLoader.item.focusContent()
                else
                    textLoader.focusWhenLoaded = true // VEDIT: still building (asynchronous)
            })
        }
    }

    Component.onCompleted: {
        root.syncSubtitlesTab()
    }

    // ListElement only accepts literal values; qsTr() calls are not
    // evaluated. Labels are translated via tabLabels below.
    property var tabLabels: ({
        "general": qsTr("General"),
        "text": qsTr("Text"),
        "shape": qsTr("Shape"),
        "vector": qsTr("Motion"),
        "model3d": qsTr("3D Model"),
        "subtitles": qsTr("Subtitles"),
        "transform": qsTr("Transform"),
        "adjust": "조정", // VEDIT
        "stabilize": qsTr("Stabilization"),
        "animation": qsTr("Animation"),
        "audio": qsTr("Audio"),
        "speed": qsTr("Speed"),
        "blending": qsTr("Blending"),
        "masks": qsTr("Masks"),
        "effects": qsTr("Effects"),
        "audioEffects": qsTr("Audio FX"),
        "transition": qsTr("Transition")
    })

    // Rail order: clip identity → geometry/timing → compositing / FX.
    // Contextual tabs (text / shape / captions) share the first group so a
    // separator still appears after General when they are hidden.
    // `group` drives hairlines between the next *visible* tab of a different group.
    ListModel {
        id: tabsModel
        ListElement { tabId: "text"; icon: 1; group: 0 }
        ListElement { tabId: "shape"; icon: 2; group: 0 }
        ListElement { tabId: "vector"; icon: 14; group: 0 }
        ListElement { tabId: "model3d"; icon: 15; group: 0 }
        ListElement { tabId: "subtitles"; icon: 3; group: 0 }
        ListElement { tabId: "transform"; icon: 4; group: 1 }
        ListElement { tabId: "adjust"; icon: 16; group: 1 } // VEDIT: CapCut Adjust
        ListElement { tabId: "stabilize"; icon: 5; group: 1 }
        ListElement { tabId: "animation"; icon: 6; group: 1 }
        ListElement { tabId: "audio"; icon: 7; group: 1 }
        ListElement { tabId: "speed"; icon: 8; group: 1 }
        ListElement { tabId: "blending"; icon: 9; group: 2 }
        ListElement { tabId: "masks"; icon: 10; group: 2 }
        ListElement { tabId: "effects"; icon: 11; group: 2 }
        ListElement { tabId: "audioEffects"; icon: 12; group: 2 }
        ListElement { tabId: "transition"; icon: 13; group: 2 }
        // VEDIT (조박사님): 일반 (the clip's details) at the far right.
        ListElement { tabId: "general"; icon: 0; group: 3 }
    }
    property var tabIcons: [
        Theme.icons.info,
        Theme.icons.type,
        Theme.icons.shapes,
        Theme.icons.captions,
        Theme.icons.maximize,
        Theme.icons.locateFixed,
        Theme.icons.sparkles,
        Theme.icons.volumeHigh,
        Theme.icons.gauge,
        Theme.icons.blend,
        Theme.icons.mask,
        Theme.icons.wand,
        Theme.icons.audioLines,
        "crossfade", // VEDIT (조박사님): 전환 — the same icon as the assets panel's 전환 tab
        Theme.icons.layers,
        Theme.icons.box,
        Theme.icons.sliders
    ]

    function tabVisible(tabId) {
        // An adjustment carries one kind of payload, so it shows the inspector for that and
        // nothing else. It used to offer Transform and Speed, which the compositor ignores for
        // an adjustment — the layer is the whole canvas, and there is no source to retime.
        if (root.clipKind === "adjustment") {
            const kind = root.clipData.adjustmentKind || "videoEffects"
            if (tabId === "general" || tabId === "blending")
                return true
            if (tabId === "effects")
                return kind === "videoEffects"
            if (tabId === "audioEffects")
                return kind === "audioEffects"
            if (tabId === "masks")
                return kind === "mask" || kind === "videoEffects"
            return false
        }
        // VEDIT (조박사님): 섞는 방식 is a section of 변형 now — its own tab only where there is no 변형.
        if (tabId === "blending")
            return false
        if (tabId === "subtitles")
            return root.clipKind === "subtitle"
        if (tabId === "shape")
            return root.clipKind === "shape"
        if (tabId === "vector")
            return root.clipKind === "vector"
        if (tabId === "model3d")
            return root.clipKind === "model3d"
        if (tabId === "text")
            return root.hasTextStyle
        if (tabId === "animation")
            return root.clipKind === "video" || root.clipKind === "image"
                   || root.clipKind === "shape" || root.clipKind === "text"
                   || root.clipKind === "vector" || root.clipKind === "audio"
                   || root.clipKind === "composite"
        if (tabId === "stabilize")
            return root.clipKind === "video"
        if (tabId === "adjust") // VEDIT
            return root.clipKind === "video" || root.clipKind === "image"
        // Masks and effect stacks live on the adjustments pinned to a clip, and those adjustments
        // are what you select to edit them. The clip is where you *aim* one from — the assets
        // panel does that — but not where it is configured: a clip can carry several mask
        // adjustments, and reporting one clip-shaped stack for it can only ever show the first.
        if (tabId === "masks" || tabId === "effects" || tabId === "audioEffects")
            return false
        return true
    }

    // Hairline after this index when the next visible tab belongs to another group.
    function showSeparatorAfter(index) {
        const cur = tabsModel.get(index)
        if (!tabVisible(cur.tabId))
            return false
        for (let i = index + 1; i < tabsModel.count; ++i) {
            const next = tabsModel.get(i)
            if (!tabVisible(next.tabId))
                continue
            return next.group !== cur.group
        }
        return false
    }

    function tabIndexOf(id) {
        for (let i = 0; i < tabsModel.count; i++) {
            if (tabsModel.get(i).tabId === id)
                return i
        }
        return -1
    }
    readonly property int subtitlesTabIndex: tabIndexOf("subtitles")
    readonly property int shapeTabIndex: tabIndexOf("shape")
    readonly property int textTabIndex: tabIndexOf("text")
    readonly property int animationTabIndex: tabIndexOf("animation")
    readonly property int stabilizeTabIndex: tabIndexOf("stabilize")
    // The one tab an adjustment exists for. Mirrors the kind switch in tabVisible above.
    readonly property string adjustmentTabId: {
        const kind = root.clipData.adjustmentKind || "videoEffects"
        if (kind === "mask")
            return "masks"
        if (kind === "audioEffects")
            return "audioEffects"
        return "effects"
    }

    // The Subtitles tab only exists for subtitle clips, so leaving it selected would show a blank
    // pane once the selection moves off one. Selecting a subtitle clip never opens the tab by
    // itself: it took over whatever pane you were working in, and with it the timeline lane.
    function syncSubtitlesTab() {
        if (root.subtitlesTabIndex >= 0 && root.activeTab === root.subtitlesTabIndex
                && root.clipKind !== "subtitle")
            root.activeTab = root.generalTabIndex
    }

    // The Shape tab is hidden for every other clip kind, so leaving it selected would show a blank
    // pane after the selection moves off a shape.
    function syncShapeTab() {
        if (root.shapeTabIndex >= 0 && root.activeTab === root.shapeTabIndex
                && root.clipKind !== "shape")
            root.activeTab = root.generalTabIndex
    }

    // Same for the Text tab, which only exists for clips carrying a text style.
    function syncTextTab() {
        if (root.textTabIndex >= 0 && root.activeTab === root.textTabIndex && !root.hasTextStyle)
            root.activeTab = root.generalTabIndex
        // VEDIT: CapCut — picking a text opens its 글자 tab, not 일반. Moving between texts keeps
        // whatever tab is open (e.g. 변형 while placing several titles).
        const kind = root.hasTextStyle && root.clipKind === "text" ? "text" : root.clipKind
        if (kind === "text" && root.lastSelectedKind !== "text" && root.textTabIndex >= 0
                && root.tabVisible("text"))
            root.activeTab = root.textTabIndex
        root.lastSelectedKind = kind
    }
    property string lastSelectedKind: "" // VEDIT

    function syncAnimationTab() {
        if (root.animationTabIndex >= 0 && root.activeTab === root.animationTabIndex
                && !root.tabVisible("animation"))
            root.activeTab = root.generalTabIndex
    }

    function syncStabilizeTab() {
        if (root.stabilizeTabIndex >= 0 && root.activeTab === root.stabilizeTabIndex
                && !root.tabVisible("stabilize"))
            root.activeTab = root.generalTabIndex
    }

    // Selecting an adjustment is how you edit its payload now, so it opens the pane that edits
    // one — the same courtesy selecting a transition already gets. A video-effects adjustment
    // also offers the Masks tab (a mask there scopes where the chain lands), but its effect
    // stack is what you came for, so kind decides and not tab order.
    function syncAdjustmentTab() {
        if (root.clipKind !== "adjustment")
            return
        const target = tabIndexOf(root.adjustmentTabId)
        if (target >= 0)
            root.activeTab = target
    }

    // The catch-all behind the per-tab syncs above: an adjustment hides most of the rail, so any
    // tab open on the clip you came from can vanish under you and leave an empty pane.
    function syncActiveTab() {
        if (!root.tabVisible(root.currentTabId))
            root.activeTab = root.generalTabIndex
    }

    // Tell the timeline to show its subtitle-cue lane only while the Subtitles tab is open.
    Binding {
        target: EditorState
        property: "subtitleEditing"
        value: root.currentTabId === "subtitles" && root.clipKind === "subtitle"
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(260, root.width - 32)
        visible: !root.hasSelection
        spacing: 16

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 48
            height: 48
            radius: Theme.radiusMd
            color: "transparent"
            border.width: 1
            border.color: Theme.panelBorder

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.sliders
                iconSize: 22
                iconColor: Theme.mutedForeground
            }
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("It's empty here")
            color: Theme.panelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
            font.weight: Font.Medium
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: root.sheetMode
                  ? qsTr("Tap a clip on the timeline to edit its properties")
                  : qsTr("Click a clip on the timeline to edit its properties")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    Item {
        id: content
        anchors.fill: parent
        visible: root.hasSelection

        // === Phone: a labelled tab strip across the top ===============================
        // The desktop rail below is a column of thirteen unlabelled icons. Inside a
        // bottom sheet it has to scroll on its own axis, stands a second scrollbar
        // next to the content's, and spends a fifth of an already narrow sheet
        // saying nothing that can be read at a glance.
        Item {
            id: tabStripHost
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            visible: root.sheetMode
            // Icon over label: the label alone identifies a tab, but the icon is
            // what the eye finds again after the first time.
            readonly property real tabChipHeight: 58
            height: visible ? tabChipHeight + Theme.spacingLg * 2 : 0

            // Keeps the selected tab on screen when it is picked from off the end,
            // and when the visible set changes with the clip kind.
            function ensureTabVisible() {
                const item = tabStripRepeater.itemAt(root.activeTab)
                if (!item || !item.visible)
                    return
                const left = tabStripRow.x + item.x
                const right = left + item.width
                const maxX = Math.max(0, tabStrip.contentWidth - tabStrip.width)
                if (left - Theme.pagePadding < tabStrip.contentX)
                    tabStrip.contentX = Math.max(0, left - Theme.pagePadding)
                else if (right + Theme.pagePadding > tabStrip.contentX + tabStrip.width)
                    tabStrip.contentX = Math.min(maxX, right + Theme.pagePadding - tabStrip.width)
            }

            Connections {
                target: root
                // callLater: a tab change can also change which tabs exist, and the
                // Row has not repositioned yet when the signal arrives.
                function onActiveTabChanged() { Qt.callLater(tabStripHost.ensureTabVisible) }
                function onClipKindChanged() { Qt.callLater(tabStripHost.ensureTabVisible) }
            }

            Flickable {
                id: tabStrip
                anchors.fill: parent
                contentWidth: tabStripRow.width + Theme.pagePadding * 2
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                Row {
                    id: tabStripRow
                    x: Theme.pagePadding
                    height: tabStrip.height
                    spacing: Theme.spacingSm

                    Repeater {
                        id: tabStripRepeater
                        model: tabsModel
                        // Not a ThemedChip: that one's contentItem is a single Text,
                        // and stacking a glyph over the label needs two rows.
                        delegate: AbstractButton {
                            id: tabChip
                            required property int index
                            required property var model

                            readonly property bool current: root.activeTab === tabChip.index

                            anchors.verticalCenter: parent.verticalCenter
                            visible: root.tabVisible(tabChip.model.tabId)
                            height: tabStripHost.tabChipHeight
                            implicitWidth: Math.max(Theme.controlHeight + Theme.spacing2xl,
                                                    tabChipLabel.implicitWidth + Theme.spacingXl * 2)
                            hoverEnabled: true
                            focusPolicy: Qt.StrongFocus

                            Accessible.role: Accessible.PageTab
                            Accessible.name: root.tabLabels[tabChip.model.tabId]
                            Accessible.checked: tabChip.current

                            scale: tabChip.down ? Theme.pressScale : 1.0

                            Behavior on scale {
                                NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
                            }

                            onClicked: {
                                Haptics.select()
                                root.activeTab = tabChip.index
                            }

                            background: Rectangle {
                                radius: Theme.radiusMd
                                color: tabChip.current
                                       ? Theme.primary
                                       : (tabChip.down ? Theme.panelMuted : Theme.panelAccent)
                                border.width: Theme.borderWidth
                                border.color: tabChip.current ? Theme.primary : Theme.panelBorder

                                Behavior on color {
                                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                }
                                Behavior on border.color {
                                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    radius: parent.radius
                                    color: "transparent"
                                    border.width: Theme.borderWidthFocus
                                    border.color: Theme.focusRing
                                    visible: tabChip.visualFocus
                                }
                            }

                            // Wrapped in an Item so the two rows sit centred in the
                            // pill; a bare Column would stack them against its top.
                            contentItem: Item {
                                Column {
                                    anchors.centerIn: parent
                                    spacing: Theme.spacingSm

                                    IconGlyph {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        glyph: root.tabIcons[tabChip.model.icon]
                                        iconSize: Theme.iconSizeLg
                                        iconColor: tabChip.current ? Theme.primaryForeground
                                                                   : Theme.panelForeground
                                    }

                                    Text {
                                        id: tabChipLabel
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        text: root.tabLabels[tabChip.model.tabId]
                                        color: tabChip.current ? Theme.primaryForeground
                                                               : Theme.panelForeground
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeXs
                                        font.weight: tabChip.current ? Font.Medium : Font.Normal
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: Theme.borderWidth
                color: Theme.panelBorder
            }
        }

        // === Desktop: vertical icon rail + tab content ================================
        // Up/Down move between tabs once the rail has focus.
        // Flickable so short panel heights can still reach lower icons.
        Flickable {
            id: propertiesTabRail
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            width: 0 // VEDIT: replaced by topTabs
            visible: false
            contentWidth: width
            contentHeight: propertiesTabRailColumn.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height
            ScrollBar.vertical: AppScrollBar {
                policy: propertiesTabRail.contentHeight > propertiesTabRail.height
                        ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            }

            Accessible.role: Accessible.PageTabList

            Keys.onUpPressed: function(event) {
                let next = root.activeTab
                for (let step = 0; step < tabsModel.count; ++step) {
                    next = (next - 1 + tabsModel.count) % tabsModel.count
                    if (root.tabVisible(tabsModel.get(next).tabId)) {
                        root.activeTab = next
                        break
                    }
                }
                event.accepted = true
            }
            Keys.onDownPressed: function(event) {
                let next = root.activeTab
                for (let step = 0; step < tabsModel.count; ++step) {
                    next = (next + 1) % tabsModel.count
                    if (root.tabVisible(tabsModel.get(next).tabId)) {
                        root.activeTab = next
                        break
                    }
                }
                event.accepted = true
            }

            Column {
                id: propertiesTabRailColumn
                width: parent.width
                topPadding: Theme.spacingSm
                spacing: Theme.spacingXs

                Repeater {
                    model: tabsModel
                    delegate: Column {
                        required property int index
                        required property var model

                        width: parent.width
                        spacing: 0
                        // Collapse so hidden contextual tabs leave no rail gap.
                        visible: root.tabVisible(model.tabId)
                        height: visible ? implicitHeight : 0

                        IconButton {
                            anchors.horizontalCenter: parent.horizontalCenter
                            glyph: root.tabIcons[model.icon]
                            variant: "ghost"
                            tooltip: root.tabLabels[model.tabId]
                            active: root.activeTab === index
                            onClicked: root.activeTab = index

                            Accessible.role: Accessible.PageTab
                            Accessible.name: root.tabLabels[model.tabId]
                            Accessible.checked: root.activeTab === index
                        }

                        // VEDIT: CapCut-style label under every rail icon, so tabs are
                        // found by name rather than by hovering for a tooltip.
                        Text {
                            width: parent.width
                            horizontalAlignment: Text.AlignHCenter
                            text: root.tabLabels[model.tabId]
                            color: root.activeTab === index ? Theme.foreground : Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            elide: Text.ElideRight
                            MouseArea {
                                anchors.fill: parent
                                onClicked: root.activeTab = index
                            }
                        }

                        Item {
                            visible: root.showSeparatorAfter(index)
                            width: parent.width
                            height: visible ? Theme.spacingLg + Theme.borderWidth : 0

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.verticalCenter: parent.verticalCenter
                                width: Theme.iconSizeSm
                                height: Theme.borderWidth
                                radius: height / 2
                                color: Theme.panelBorder
                                opacity: 0.85
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            id: railDivider
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.left: propertiesTabRail.right
            width: 0 // VEDIT
            visible: false
            color: Theme.panelBorder
        }

        // VEDIT: CapCut-style tabs across the top instead of the vertical rail (조박사님).
        TopTabStrip {
            id: topTabs
            anchors.top: tabStripHost.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            visible: !root.sheetMode
            height: visible ? implicitHeight : 0
            model: tabsModel
            icons: root.tabIcons
            labels: root.tabLabels
            tabVisible: function(tabId) { return root.tabVisible(tabId) }
            currentIndex: root.activeTab
            onTabClicked: function(index) { root.activeTab = index }
        }

        Flickable {
            id: tabFlick
            anchors.top: topTabs.bottom
            anchors.left: railDivider.right
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: root.currentTabId !== "subtitles"
            contentWidth: width
            // Include topPadding so the last controls stay reachable (SettingsTab pattern).
            contentHeight: tabColumn.height + Theme.spacing3xl
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height
            ScrollBar.vertical: AppScrollBar {
                policy: tabFlick.contentHeight > tabFlick.height
                        ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            }

            // Tall tabs (Audio/Effects) leave contentY deep; reset when switching.
            Connections {
                target: root
                function onActiveTabChanged() {
                    tabFlick.contentY = 0
                    tabColumn.opacity = 0
                    tabFadeIn.restart()
                }
            }

            // Fade the new tab in rather than hard-cutting to it. Fade-in only,
            // not a crossfade: the inspectors share a Column, which excludes
            // invisible items from layout, so overlapping two of them would
            // double-count height and jump the panel mid-transition.
            NumberAnimation {
                id: tabFadeIn
                target: tabColumn
                property: "opacity"
                from: 0.0
                to: 1.0
                duration: Theme.durationBase
                easing.type: Theme.easing
            }

            Column {
                id: tabColumn
                x: Theme.pagePadding
                width: parent.width - Theme.pagePadding * 2
                // Sections need to read apart on a phone, where the whole pane is
                // about two of them tall.
                spacing: root.sheetMode ? Theme.spacing2xl : Theme.spacingXl
                topPadding: root.sheetMode ? Theme.spacing2xl : Theme.pagePadding

                Text {
                    // The strip above already names the tab, in a size you can read.
                    visible: !root.sheetMode
                    text: root.tabLabels[tabsModel.get(root.activeTab).tabId]
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.Medium
                }

                // Only the open tab is instantiated. Hidden inspectors used to stay alive and
                // re-read the whole selected clip on every edit; the view state that has to
                // outlive a tab switch is kept in inspectorUi instead.
                Loader {
                    active: root.currentTabId === "general"
                    visible: active
                    sourceComponent: Component { GeneralInspector { width: tabColumn.width } }
                }

                Loader {
                    id: textLoader
                    active: root.currentTabId === "text"
                    visible: active
                    // VEDIT (조박사님: "텍스트 클립을 선택하면 너무 늦게 선택된다"): the text page is
                    // big; built in the background, the clip shows as picked at once and the page
                    // fills in a moment later.
                    asynchronous: true
                    property bool focusWhenLoaded: false
                    onLoaded: {
                        if (focusWhenLoaded) {
                            focusWhenLoaded = false
                            item.focusContent()
                        }
                    }
                    sourceComponent: Component {
                        TextInspector {
                            width: tabColumn.width
                            textTab: inspectorUi.textTab
                            animSlot: inspectorUi.animSlot
                            animCategory: inspectorUi.animCategory
                            expandedLayerIds: inspectorUi.textExpandedLayerIds
                            contentFieldHeight: inspectorUi.contentFieldHeight
                            onTextTabChanged: inspectorUi.textTab = textTab
                            onAnimSlotChanged: inspectorUi.animSlot = animSlot
                            onAnimCategoryChanged: inspectorUi.animCategory = animCategory
                            onExpandedLayerIdsChanged: inspectorUi.textExpandedLayerIds = expandedLayerIds
                            onContentFieldHeightChanged: inspectorUi.contentFieldHeight = contentFieldHeight
                        }
                    }
                }

                Loader {
                    active: root.currentTabId === "transform"
                    visible: active
                    sourceComponent: Component {
                        Column {
                            width: tabColumn.width
                            spacing: Theme.spacingXl
                            TransformInspector {
                                width: tabColumn.width
                                sizeLinked: inspectorUi.sizeLinked
                                onSizeLinkedChanged: inspectorUi.sizeLinked = sizeLinked
                            }
                            // VEDIT (조박사님, CapCut): 섞는 방식 lives inside 변형 as its 혼합 section.
                            Column {
                                id: blendSection
                                visible: root.clipKind !== "audio"
                                width: tabColumn.width
                                spacing: 8
                                property bool open: true
                                Rectangle { width: parent.width; height: 1; color: Theme.panelBorder }
                                Item {
                                    width: parent.width
                                    height: 28
                                    Row {
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 6
                                        IconGlyph {
                                            anchors.verticalCenter: parent.verticalCenter
                                            glyph: blendSection.open ? Theme.icons.chevronDown : Theme.icons.chevronRight
                                            iconSize: 14
                                            iconColor: Theme.mutedForeground
                                        }
                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: qsTr("Blend")
                                            color: Theme.panelForeground
                                            font.family: Theme.fontFamily
                                            font.pixelSize: Theme.fontSizeSm
                                            font.weight: Font.Medium
                                        }
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: blendSection.open = !blendSection.open
                                    }
                                }
                                BlendingInspector {
                                    visible: blendSection.open
                                    width: tabColumn.width
                                }
                            }
                        }
                    }
                }

                Loader {
                    active: root.currentTabId === "stabilize"
                    visible: active
                    sourceComponent: Component { StabilizeInspector { width: tabColumn.width } }
                }

                Loader {
                    active: root.currentTabId === "animation"
                    visible: active
                    sourceComponent: Component { AnimationInspector { width: tabColumn.width } }
                }

                Loader {
                    active: root.currentTabId === "audio"
                    visible: active
                    sourceComponent: Component { AudioInspector { width: tabColumn.width } }
                }

                Loader { // VEDIT: CapCut Adjust
                    active: root.currentTabId === "adjust"
                    visible: active
                    sourceComponent: Component { AdjustInspector { width: tabColumn.width } }
                }

                Loader {
                    active: root.currentTabId === "speed"
                    visible: active
                    sourceComponent: Component { SpeedFadeInspector { width: tabColumn.width } }
                }

                // Eager: onActiveTabChanged commits its fields on the way out, which a Loader
                // could already have destroyed.
                TransitionInspector {
                    id: transitionInspector
                    width: tabColumn.width
                    visible: root.currentTabId === "transition"
                }

                Loader {
                    active: root.currentTabId === "blending"
                    visible: active
                    sourceComponent: Component { BlendingInspector { width: tabColumn.width } }
                }

                Loader {
                    active: root.currentTabId === "shape"
                    visible: active
                    sourceComponent: Component {
                        ShapeInspector {
                            width: tabColumn.width
                            expandedLayerIds: inspectorUi.shapeExpandedLayerIds
                            onExpandedLayerIdsChanged: inspectorUi.shapeExpandedLayerIds = expandedLayerIds
                        }
                    }
                }

                Loader {
                    active: root.currentTabId === "vector"
                    visible: active
                    sourceComponent: Component { VectorInspector { width: tabColumn.width } }
                }

                Loader {
                    active: root.currentTabId === "model3d"
                    visible: active
                    sourceComponent: Component { Model3DInspector { width: tabColumn.width } }
                }

                Loader {
                    active: root.currentTabId === "masks"
                    visible: active
                    sourceComponent: Component { MasksInspector { width: tabColumn.width } }
                }

                Loader {
                    active: root.currentTabId === "effects"
                    visible: active
                    sourceComponent: Component {
                        EffectsInspector {
                            width: tabColumn.width
                            onBrowseEffectsRequested: root.browseEffectsRequested()
                            onSaveEffectPresetRequested: function(effectIndex) {
                                root.savePresetEffectIndex = effectIndex
                                effectPresetNameDialog.openWith(
                                    effectIndex < 0 ? qsTr("Save effect preset")
                                                    : qsTr("Save effect as preset"),
                                    root.hasSelection ? (root.clipData.name || "") : "")
                            }
                        }
                    }
                }

                Loader {
                    active: root.currentTabId === "audioEffects"
                    visible: active
                    sourceComponent: Component {
                        AudioEffectsInspector {
                            width: tabColumn.width
                            onBrowseAudioEffectsRequested: root.browseAudioEffectsRequested()
                        }
                    }
                }
            }
        }

        // Full-height editor with its own internal cue list scrolling, so it
        // sits beside the tab Flickable rather than inside it.
        SubtitleEditor {
            anchors.top: tabStripHost.bottom
            anchors.left: railDivider.right
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: root.currentTabId === "subtitles"
            clip: root.hasSelection ? root.clipData : null
            formatSeconds: root.formatSeconds
        }
    }

    NameDialog {
        id: effectPresetNameDialog
        placeholder: qsTr("My look")
        onSubmitted: function(name) {
            if (root.savePresetEffectIndex < 0)
                EditorState.saveClipEffectsAsPreset(EditorState.selectedTrack,
                                                    EditorState.selectedClip, name)
            else
                EditorState.saveEffectAsPreset(EditorState.selectedTrack,
                                               EditorState.selectedClip,
                                               root.savePresetEffectIndex, name)
            root.savePresetEffectIndex = -1
        }
        onRejected: root.savePresetEffectIndex = -1
    }
}
