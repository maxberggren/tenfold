import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Tenfold

ApplicationWindow {
    id: win
    width: 1320
    height: 900
    minimumWidth: 900
    minimumHeight: 620
    visible: true
    title: "tenfold"
    color: "#0b0b0e"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    readonly property bool playing: backend.padState === Backend.Playing

    onActiveChanged: backend.windowActive(active)

    function quit() {
        if (backend.recording)
            quitDialog.open()
        else
            Qt.quit()
    }
    onClosing: close => {
        if (backend.recording) {
            close.accepted = false
            quitDialog.open()
        }
    }

    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; onActivated: backend.togglePlay() }
    Shortcut {
        sequence: "Esc"; context: Qt.ApplicationShortcut
        onActivated: win.helpVisible ? win.helpVisible = false : backend.giveBack()
    }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; onActivated: win.quit() }
    Shortcut { sequence: "Right"; context: Qt.ApplicationShortcut; onActivated: backend.shiftRoot(1) }
    Shortcut { sequence: "Left"; context: Qt.ApplicationShortcut; onActivated: backend.shiftRoot(-1) }
    Shortcut { sequence: "Up"; context: Qt.ApplicationShortcut; onActivated: backend.shiftFamily(1) }
    Shortcut { sequence: "Down"; context: Qt.ApplicationShortcut; onActivated: backend.shiftFamily(-1) }
    Shortcut { sequence: "J"; context: Qt.ApplicationShortcut; onActivated: backend.toggleJust() }
    Shortcut { sequences: ["PgUp", "]"]; context: Qt.ApplicationShortcut; onActivated: backend.shiftOctave(1) }
    Shortcut { sequences: ["PgDown", "["]; context: Qt.ApplicationShortcut; onActivated: backend.shiftOctave(-1) }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; onActivated: backend.toggleRecording() }
    Shortcut { sequence: "Backspace"; context: Qt.ApplicationShortcut; onActivated: backend.dissolve() }
    Shortcut { sequence: "Shift+Backspace"; context: Qt.ApplicationShortcut; onActivated: backend.dissolveAll() }
    Shortcut { sequence: "0"; context: Qt.ApplicationShortcut; onActivated: backend.resetKnobs() }
    Shortcut {
        sequence: "F"; context: Qt.ApplicationShortcut
        onActivated: win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }

    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 22
                Layout.rightMargin: 22
                Layout.topMargin: 14
                spacing: 18

                Label {
                    text: "tenfold"
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    color: theme.accent
                }
                Label {
                    text: qsTr("%1 %2").arg(backend.key).arg(backend.mode)
                    font.pixelSize: 15
                    color: "#e6e6e6"
                }
                Label {
                    visible: backend.octave !== 0
                    text: qsTr("octave %1").arg((backend.octave > 0 ? "+" : "") + backend.octave)
                    font.pixelSize: 13
                    color: theme.accent
                }
                Label {
                    text: backend.just ? qsTr("just intonation") : qsTr("equal temperament")
                    font.pixelSize: 13
                    color: "#8a8a92"
                }
                Label {
                    text: backend.tempoSet
                          ? qsTr("%1 BPM").arg(backend.bpm.toLocaleString(Qt.locale(), 'f', 1))
                          : qsTr("tempo: your first loop")
                    font.pixelSize: 13
                    color: "#8a8a92"
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: qsTr("%n loops", "", backend.loops) + " · " + qsTr("%n held", "", backend.voices)
                          + " · " + qsTr("%n frozen", "", backend.frozen)
                    font.pixelSize: 13
                    color: "#8a8a92"
                }
                Label {
                    text: qsTr("DSP %1%").arg((backend.cpu * 100).toLocaleString(Qt.locale(), 'f', 0))
                          + (backend.latencyMs > 0 ? " · " + qsTr("%1 ms").arg(backend.latencyMs.toLocaleString(Qt.locale(), 'f', 1)) : "")
                    font.pixelSize: 13
                    color: "#8a8a92"
                }
                Rectangle {
                    visible: backend.recording
                    radius: height / 2
                    color: "#d0312d"
                    implicitWidth: recLabel.implicitWidth + 18
                    implicitHeight: recLabel.implicitHeight + 6
                    Label {
                        id: recLabel
                        anchors.centerIn: parent
                        text: qsTr("REC %1").arg(new Date(backend.recordSeconds * 1000).toISOString().substr(14, 5))
                        font.pixelSize: 12
                        color: "white"
                    }
                }
                Label {
                    text: win.playing ? qsTr("Esc gives the trackpad back") : qsTr("Space to play")
                    font.pixelSize: 13
                    color: win.playing ? theme.accent : "#e6e6e6"
                }
                Label {
                    text: qsTr("? help")
                    font.pixelSize: 13
                    color: "#8a8a92"
                }
            }

            Surface {
                id: surface
                Layout.fillWidth: true
                Layout.fillHeight: true
                app: backend
                accent: theme.accent
            }
        }

        // Not playing: say how to start and what is about to happen.
        Rectangle {
            anchors.centerIn: parent
            visible: !win.playing && !win.helpVisible
            width: Math.min(parent.width - 80, 560)
            height: startColumn.implicitHeight + 48
            radius: theme.radius
            color: "#e6141418"
            border.color: Qt.rgba(1, 1, 1, 0.08)

            ColumnLayout {
                id: startColumn
                anchors.fill: parent
                anchors.margins: 24
                spacing: 12

                Label {
                    Layout.fillWidth: true
                    text: backend.padState === Backend.Starting ? qsTr("Waiting for the trackpad…")
                        : backend.padState === Backend.Failed ? qsTr("The trackpad can't be played")
                        : backend.padState === Backend.Ready ? qsTr("Paused")
                        : qsTr("Play with ten fingers")
                    font.pixelSize: 22
                    font.weight: Font.DemiBold
                    color: "#f2f2f2"
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    visible: backend.padState === Backend.Failed
                    text: backend.error + " " + qsTr("Press Space to try again.")
                    color: "#ff8a80"
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    visible: !backend.audioOk
                    text: qsTr("No sound: %1").arg(backend.audioError)
                    color: "#ff8a80"
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    text: backend.padState === Backend.Ready
                          ? qsTr("The trackpad is yours again and the sound keeps playing. Space takes it back.")
                          : qsTr("Space hands the trackpad to tenfold: the pointer stops and every finger plays. Reading the pad needs your password once. Esc gives it back at any time.")
                    color: "#b8b8c0"
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Tap to strike. Hold to bloom a voice. Tap a rhythm and hold on the next beat to loop it. Press down to keep what you hold.")
                    color: "#8a8a92"
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    visible: backend.lastRecording !== ""
                    text: qsTr("Saved %1").arg(backend.lastRecording)
                    color: "#8a8a92"
                    elide: Text.ElideMiddle
                }
            }
        }

        // What a swipe just changed, big for a moment.
        Label {
            id: noticeLabel
            anchors.centerIn: parent
            text: backend.notice
            font.pixelSize: 64
            font.weight: Font.DemiBold
            color: theme.accent
            opacity: 0
            Connections {
                target: backend
                function onNoticeChanged() { noticeFade.restart() }
            }
            SequentialAnimation {
                id: noticeFade
                NumberAnimation { target: noticeLabel; property: "opacity"; to: 0.95; duration: 80 }
                PauseAnimation { duration: 700 }
                NumberAnimation { target: noticeLabel; property: "opacity"; to: 0; duration: 500 }
            }
        }

        GestureGuide {
            anchors.fill: parent
            visible: win.helpVisible
        }
    }

    Dialog {
        id: quitDialog
        anchors.centerIn: parent
        modal: true
        title: qsTr("Stop recording and quit?")
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: Qt.quit()
        Label { text: qsTr("The recording so far is saved.") }
    }
}
