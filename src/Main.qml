import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Tenfold

// The window is a console: phosphor outlines on black glass, a different
// phosphor for each kind of thing. Amber is the panel itself, green is
// "ready", cyan is the scale, magenta the loops, red recording and alarms.
ApplicationWindow {
    id: win
    width: 1440
    height: 940
    minimumWidth: 1100
    minimumHeight: 720
    visible: true
    title: "tenfold"
    color: "#000000"

    Material.theme: Material.Dark
    Material.accent: amber
    Material.roundedScale: Material.NotRounded

    readonly property color amber: "#FFB000"
    readonly property color green: "#39FF88"
    readonly property color cyan: "#2EE6FF"
    readonly property color magenta: "#FF4FD8"
    readonly property color red: "#FF4136"
    readonly property color orange: "#FF8A1F"
    readonly property string wide: "JetBrainsMono Nerd Font"
    readonly property string mono: "Noto Sans Mono"

    property bool helpVisible: false
    readonly property bool playing: backend.padState === Backend.Playing
    property date now: new Date()
    property double started: Date.now()

    onActiveChanged: backend.windowActive(active)

    Timer {
        interval: 500
        running: true
        repeat: true
        onTriggered: win.now = new Date()
    }

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
    function pad2(n) { return (n < 10 ? "0" : "") + n }
    function pad4(n) { return ("000" + n).slice(-4) }

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
        id: screen
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 10

            // Wordmark, mode line, and the two console buttons.
            RowLayout {
                Layout.fillWidth: true
                spacing: 14

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: "TENFOLD"
                        color: win.amber
                        font.family: win.wide
                        font.pixelSize: 34
                        font.weight: Font.Black
                        font.letterSpacing: 6
                    }
                    Text {
                        text: win.playing ? qsTr("Pad live — play mode") : qsTr("Pointer — standby mode")
                        color: win.amber
                        font.family: win.wide
                        font.pixelSize: 24
                        font.weight: Font.Bold
                        font.letterSpacing: 3
                        font.capitalization: Font.AllUppercase
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        height: 3
                        color: win.amber
                    }
                }
                PanelButton {
                    Layout.preferredWidth: 170
                    Layout.preferredHeight: 72
                    Layout.alignment: Qt.AlignTop
                    label: qsTr("Gestures")
                    key: "?"
                    color: win.cyan
                    active: win.helpVisible
                    onClicked: win.helpVisible = !win.helpVisible
                }
                PanelButton {
                    Layout.preferredWidth: 170
                    Layout.preferredHeight: 72
                    Layout.alignment: Qt.AlignTop
                    label: backend.recording
                           ? qsTr("Rec %1").arg(new Date(backend.recordSeconds * 1000).toISOString().substr(14, 5))
                           : qsTr("Record")
                    key: "R"
                    color: win.red
                    active: backend.recording
                    onClicked: backend.toggleRecording()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 16

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 12

                    Surface {
                        id: surface
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        app: backend
                        accent: win.amber
                    }

                    // The mode banner.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 54
                        color: "transparent"
                        border.color: win.playing ? win.green : win.amber
                        border.width: 3
                        Text {
                            anchors.centerIn: parent
                            text: win.playing ? "**  " + qsTr("Pad live") + "  **" : "**  " + qsTr("Pointer mode") + "  **"
                            color: win.playing ? win.green : win.amber
                            font.family: win.wide
                            font.pixelSize: 30
                            font.weight: Font.Black
                            font.letterSpacing: 8
                            font.capitalization: Font.AllUppercase
                        }
                    }
                }

                // The right-hand column: what the instrument is set to, what's
                // ready, its readouts, and its buttons.
                ColumnLayout {
                    Layout.preferredWidth: 340
                    Layout.maximumWidth: 340
                    Layout.fillHeight: true
                    spacing: 12

                    PanelBox {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 104
                        title: qsTr("Scale information")
                        color: win.cyan
                        Column {
                            spacing: 4
                            Text {
                                text: qsTr("Key: %1").arg(backend.key)
                                color: win.cyan
                                font.family: win.mono
                                font.pixelSize: 19
                                font.weight: Font.DemiBold
                                font.capitalization: Font.AllUppercase
                            }
                            Text {
                                text: qsTr("Mode: %1").arg(backend.mode)
                                color: win.cyan
                                font.family: win.mono
                                font.pixelSize: 15
                                font.capitalization: Font.AllUppercase
                            }
                            Text {
                                text: (backend.just ? qsTr("Just intonation") : qsTr("Equal temperament"))
                                      + "  ·  " + qsTr("Oct %1").arg((backend.octave > 0 ? "+" : "") + backend.octave)
                                color: win.cyan
                                font.family: win.mono
                                font.pixelSize: 13
                                font.capitalization: Font.AllUppercase
                            }
                        }
                    }

                    Column {
                        Layout.fillWidth: true
                        spacing: 6
                        StatusLamp { label: qsTr("Trackpad grabbed"); on: win.playing; color: win.green }
                        StatusLamp { label: qsTr("Audio running"); on: backend.audioOk; color: win.green }
                        StatusLamp { label: qsTr("Tempo locked"); on: backend.tempoSet; color: win.magenta }
                        StatusLamp { label: qsTr("Recording"); on: backend.recording; color: win.red }
                        StatusLamp { label: qsTr("Palm damper"); on: backend.damper > 0.05; color: win.orange }
                    }

                    // Readouts: an inverse title bar over a large value.
                    Repeater {
                        model: 2
                        delegate: Column {
                            id: readout
                            Layout.fillWidth: true
                            spacing: 0
                            readonly property color tint: [win.green, win.magenta, win.amber][index]
                            readonly property string heading: [qsTr("DSP values"), qsTr("Tempo"), qsTr("Landscape")][index]
                            readonly property string value: index === 0
                                ? (backend.cpu * 100).toFixed(0) + " %   " + (backend.latencyMs > 0 ? backend.latencyMs.toFixed(1) + " MS" : "— MS")
                                : index === 1
                                  ? (backend.tempoSet ? backend.bpm.toFixed(1) + " BPM" : qsTr("Free"))
                                  : qsTr("%1 loops  %2 held  %3 frozen").arg(backend.loops).arg(backend.voices).arg(backend.frozen)
                            Rectangle {
                                width: parent.width
                                height: 22
                                color: readout.tint
                                Text {
                                    anchors.centerIn: parent
                                    text: readout.heading
                                    color: "#0a0806"
                                    font.family: win.mono
                                    font.pixelSize: 14
                                    font.weight: Font.Bold
                                    font.letterSpacing: 1.5
                                    font.capitalization: Font.AllUppercase
                                }
                            }
                            Rectangle {
                                width: parent.width
                                height: 38
                                color: "transparent"
                                border.color: readout.tint
                                border.width: 2
                                Text {
                                    anchors.centerIn: parent
                                    text: readout.value
                                    color: readout.tint
                                    font.family: win.mono
                                    font.pixelSize: index === 2 ? 17 : 24
                                    font.weight: Font.DemiBold
                                    font.capitalization: Font.AllUppercase
                                }
                            }
                        }
                    }

                    GridLayout {
                        id: buttons
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.topMargin: 6
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 8
                        property int bh: 46
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            label: win.playing ? qsTr("Release") : qsTr("Play")
                            key: "Space"; color: win.green; active: win.playing
                            onClicked: backend.togglePlay()
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            label: qsTr("Dissolve"); key: "⌫"; color: win.magenta
                            onClicked: backend.dissolve()
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            label: qsTr("Key ◀"); key: "←"; color: win.cyan
                            onClicked: backend.shiftRoot(-1)
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            label: qsTr("Key ▶"); key: "→"; color: win.cyan
                            onClicked: backend.shiftRoot(1)
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            label: qsTr("Oct −"); key: "["; color: win.cyan
                            onClicked: backend.shiftOctave(-1)
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            label: qsTr("Oct +"); key: "]"; color: win.cyan
                            onClicked: backend.shiftOctave(1)
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            label: qsTr("Scale"); key: "↑ ↓"; color: win.cyan
                            onClicked: backend.shiftFamily(1)
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            label: backend.just ? qsTr("Just") : qsTr("Equal"); key: "J"; color: win.cyan
                            active: backend.just
                            onClicked: backend.toggleJust()
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            label: qsTr("Reset knobs"); key: "0"; color: win.amber
                            onClicked: backend.resetKnobs()
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            label: qsTr("Clear all"); key: "⇧⌫"; color: win.red
                            onClicked: backend.dissolveAll()
                        }
                    }
                }
            }

            // Status and clock, in inverse video across the bottom.
            Column {
                Layout.fillWidth: true
                spacing: 4
                Rectangle {
                    width: parent.width
                    height: 28
                    color: win.amber
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 16
                        width: parent.width - 32
                        elide: Text.ElideRight
                        text: qsTr("Status: %1").arg(
                                  backend.padState === Backend.Playing ? qsTr("playing")
                                : backend.padState === Backend.Starting ? qsTr("waiting for the trackpad")
                                : backend.padState === Backend.Failed ? qsTr("fault — %1").arg(backend.error)
                                : backend.padState === Backend.Ready ? qsTr("paused, pointer released")
                                : qsTr("standby, press Space"))
                              + "     " + qsTr("Landscape: %1 loops · %2 held · %3 frozen").arg(backend.loops).arg(backend.voices).arg(backend.frozen)
                              + (backend.audioOk ? "" : "     " + qsTr("No sound: %1").arg(backend.audioError))
                        color: "#0a0806"
                        font.family: win.mono
                        font.pixelSize: 16
                        font.weight: Font.Bold
                        font.letterSpacing: 1
                        font.capitalization: Font.AllUppercase
                    }
                }
                Rectangle {
                    width: parent.width
                    height: 28
                    color: Qt.darker(win.amber, 1.25)
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 16
                        width: parent.width - 32
                        elide: Text.ElideMiddle
                        text: {
                            const s = Math.max(0, Math.floor((win.now.getTime() - win.started) / 1000))
                            return qsTr("Show time %1:%2 (MM:SS)").arg(win.pad4(Math.floor(s / 60))).arg(win.pad2(s % 60))
                                   + "     " + qsTr("Date %1").arg(Qt.formatDate(win.now, "MM/dd/yy"))
                                   + "     " + qsTr("Time %1").arg(Qt.formatTime(win.now, "HH:mm:ss"))
                                   + (backend.lastRecording !== "" ? "     " + qsTr("Saved %1").arg(backend.lastRecording) : "")
                        }
                        color: "#0a0806"
                        font.family: win.mono
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                        font.capitalization: Font.AllUppercase
                    }
                }
            }
        }

        // Not playing yet: a console dialog that says what Space will do.
        Rectangle {
            anchors.centerIn: parent
            visible: !win.playing && !win.helpVisible
            width: Math.min(parent.width - 120, 620)
            height: startColumn.implicitHeight + 64
            color: "#000000"
            border.color: backend.padState === Backend.Failed ? win.red : win.amber
            border.width: 3

            Rectangle {
                width: parent.width
                height: 34
                color: backend.padState === Backend.Failed ? win.red : win.amber
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 14
                    text: backend.padState === Backend.Starting ? qsTr("Waiting for the trackpad")
                        : backend.padState === Backend.Failed ? qsTr("Alarm — trackpad unavailable")
                        : backend.padState === Backend.Ready ? qsTr("Paused")
                        : qsTr("Standby")
                    color: "#0a0806"
                    font.family: win.mono
                    font.pixelSize: 17
                    font.weight: Font.Bold
                    font.letterSpacing: 2
                    font.capitalization: Font.AllUppercase
                }
            }

            ColumnLayout {
                id: startColumn
                anchors.fill: parent
                anchors.margins: 22
                anchors.topMargin: 52
                spacing: 12

                Text {
                    Layout.fillWidth: true
                    visible: backend.padState === Backend.Failed
                    text: backend.error + " " + qsTr("Press Space to try again.")
                    color: win.red
                    font.family: win.mono
                    font.pixelSize: 15
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: backend.padState === Backend.Ready
                          ? qsTr("The trackpad is yours again and the sound keeps playing. Space takes it back.")
                          : qsTr("Space hands the trackpad to tenfold: the pointer stops and every finger plays. Reading the pad needs your password once. Esc gives it back at any time.")
                    color: win.amber
                    font.family: win.mono
                    font.pixelSize: 15
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Tap to strike. Hold to bloom a voice. Tap a rhythm and hold on the next beat to loop it. Press down to keep what you hold.")
                    color: win.cyan
                    font.family: win.mono
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }
            }
        }

        // What a swipe just changed, big for a moment.
        Text {
            id: noticeLabel
            anchors.centerIn: parent
            text: backend.notice
            font.family: win.wide
            font.pixelSize: 72
            font.weight: Font.Black
            font.letterSpacing: 6
            font.capitalization: Font.AllUppercase
            color: win.cyan
            opacity: 0
            Connections {
                target: backend
                function onNoticeChanged() { noticeFade.restart() }
            }
            SequentialAnimation {
                id: noticeFade
                NumberAnimation { target: noticeLabel; property: "opacity"; to: 1; duration: 60 }
                PauseAnimation { duration: 700 }
                NumberAnimation { target: noticeLabel; property: "opacity"; to: 0; duration: 500 }
            }
        }

        GestureGuide {
            anchors.fill: parent
            visible: win.helpVisible
        }
    }

    // Scanlines over the glass.
    Canvas {
        anchors.fill: parent
        opacity: 0.22
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            ctx.fillStyle = "#000000"
            for (let y = 0; y < height; y += 3)
                ctx.fillRect(0, y, width, 1)
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
