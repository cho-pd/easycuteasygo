import QtQuick
import QtQuick.Controls.Basic
import Drift

// Shows live progress for a background export. Closable while the export keeps
// running; EditorHeader reopens it via the circular-progress badge next to the
// Export button when it's been dismissed mid-export. Cancel stops the encoder.
ThemedDialog {
    id: root

    title: EditorState.exportInProgress ? qsTr("Exporting video") : qsTr("Export")
    preferredWidth: Theme.dialogWidthSm
    // Cancel export is the primary destructive action while busy; Close dismisses
    // the dialog without stopping the job (badge in the header reopens it).
    showAccept: EditorState.exportInProgress
    acceptText: qsTr("Cancel export")
    acceptVariant: "destructive"
    acceptOnReturn: false
    rejectText: qsTr("Close")
    rejectVariant: "secondary"

    function openDialog() {
        open()
    }

    onAccepted: {
        if (EditorState.exportInProgress)
            EditorState.cancelExport()
    }

    contentItem: Column {
        spacing: Theme.spacing2xl
        width: parent ? parent.width : 308

        LabelledProgressRing {
            width: parent.width
            value: EditorState.exportProgress
            indeterminate: EditorState.exportInProgress && EditorState.exportProgress <= 0
        }

        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            size: "sm"
            wrapMode: Text.WordWrap
            text: EditorState.exportInProgress
                  ? qsTr("Rendering your video. Close to keep editing, or cancel to stop.")
                  : qsTr("Export finished.")
        }

        // VEDIT: time left and size so far; afterwards the final size and how long it took.
        ThemedLabel {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            visible: text.length > 0
            wrapMode: Text.WordWrap
            text: EditorState.exportStatusText
        }

        // The finished state used to be that sentence and nothing else — the file was on the
        // device and every way to it was outside the app. Both actions publish to the gallery
        // first, which is a second full copy of the video and the reason it is not done as part
        // of the export.
        //
        // Bound to canShareExport rather than to "the export ended": the publish runs on a
        // worker, so it is false again for as long as either button's copy is in flight, and
        // that is exactly when neither should be pressable.
        // VEDIT: desktop — show where the video went, or play it, right from the finished dialog.
        Row {
            width: parent.width
            spacing: Theme.spacingMd
            visible: Qt.platform.os !== "android" && !EditorState.exportInProgress
                     && EditorState.exportProgress >= 1.0 && EditorState.hasLastExport()

            ThemedButton {
                width: (parent.width - parent.spacing) / 2
                variant: "primary"
                glyph: Theme.icons.folder
                text: "폴더 열기"
                onClicked: EditorState.revealLastExport()
            }

            ThemedButton {
                width: (parent.width - parent.spacing) / 2
                variant: "secondary"
                glyph: Theme.icons.play
                text: "바로 보기"
                onClicked: EditorState.openLastExport()
            }
        }

        Row {
            width: parent.width
            spacing: Theme.androidTouchGap
            visible: !EditorState.exportInProgress && EditorState.canShareExport

            ThemedButton {
                width: (parent.width - parent.spacing) / 2
                height: Theme.androidMinTouchTarget
                variant: "secondary"
                glyph: Theme.icons.play
                text: qsTr("Play")
                onClicked: EditorState.playLastExport()
            }

            ThemedButton {
                width: (parent.width - parent.spacing) / 2
                height: Theme.androidMinTouchTarget
                variant: "primary"
                glyph: Theme.icons.upload
                text: qsTr("Share")
                onClicked: EditorState.shareLastExport()
            }
        }
    }
}
