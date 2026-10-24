import QtQuick
import QtQuick.Controls.Basic
import Drift

// VEDIT: 소리로 맞추기 진행 — shown while the clips' sound is read and compared (a long concert
// recording takes a while). A turning ring with the percentage, and a way to stop.
ThemedDialog {
    id: root

    title: qsTr("Lining up by sound")
    preferredWidth: 380
    showFooter: false
    showAccept: false
    showReject: false
    acceptOnReturn: false
    closePolicy: Popup.CloseOnEscape

    onClosed: {
        if (EditorState.audioSyncBusy)
            EditorState.cancelAudioSync()
    }

    Connections {
        target: EditorState
        function onAudioSyncChanged() {
            if (EditorState.audioSyncBusy && !root.visible)
                root.open()
            else if (!EditorState.audioSyncBusy && root.visible)
                root.close()
        }
    }

    contentItem: Column {
        spacing: Theme.spacing2xl
        width: parent ? parent.width : 348

        LabelledProgressRing {
            width: parent.width
            value: EditorState.audioSyncProgress
            indeterminate: EditorState.audioSyncProgress <= 0.001
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("Listening to the clips and lining them up… Long recordings take a little while.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: 16
        }

        ThemedButton {
            anchors.horizontalCenter: parent.horizontalCenter
            variant: "secondary"
            text: qsTr("Stop")
            onClicked: EditorState.cancelAudioSync()
        }
    }
}
