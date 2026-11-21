import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Tenfold

// The window is a console: phosphor outlines on black glass. Each part has
// its own phosphor, and its page fills the glass when its number is pressed;
// amber is the panel itself, green "ready", red recording and alarms.
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
    readonly property color red: "#FF4136"
    readonly property string wide: "JetBrainsMono Nerd Font"
    readonly property string mono: "Noto Sans Mono"

    property bool helpVisible: false
    readonly property bool playing: backend.padState === Backend.Playing
    readonly property color pageColor: backend.pageColors[backend.page]

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
    // The number a page answers to: 1–7 for the parts, 0 for arrange.
    function pageKey(i) { return i === backend.pageCount - 1 ? "0" : String(i + 1) }

    Shortcut {
        sequence: "Esc"; context: Qt.ApplicationShortcut
        // Closes what is open first: the guide, then the tutorial; then it
        // gives the trackpad back.
        onActivated: win.helpVisible ? win.helpVisible = false
                   : backend.tutorialActive ? backend.toggleTutorial() : backend.giveBack()
    }
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Ctrl+Q"; context: Qt.ApplicationShortcut; onActivated: win.quit() }
    Shortcut { sequence: "Ctrl+R"; context: Qt.ApplicationShortcut; onActivated: backend.toggleRecording() }
    Shortcut {
        sequence: "F11"; context: Qt.ApplicationShortcut
        onActivated: win.visibility = win.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
    }

    Item {
        id: screen
        anchors.fill: parent
        focus: true
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        // Every other key goes to the session: page numbers, tempo and key on
        // Ctrl, and whatever the showing page plays from the keyboard.
        Keys.onPressed: event => {
            event.accepted = backend.sendKey(event.key, event.modifiers, true, event.isAutoRepeat, event.text)
        }
        Keys.onReleased: event => {
            event.accepted = backend.sendKey(event.key, event.modifiers, false, event.isAutoRepeat, event.text)
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 10

            // Wordmark and the pages, one tab each, numbered as the keys are.
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Text {
                    Layout.alignment: Qt.AlignVCenter
                    Layout.rightMargin: 8
                    text: "TENFOLD"
                    color: win.amber
                    font.family: win.wide
                    font.pixelSize: 30
                    font.weight: Font.Black
                    font.letterSpacing: 5
                }
                Repeater {
                    model: backend.pageCount
                    delegate: Item {
                        id: tab
                        required property int index
                        readonly property color tint: backend.pageColors[index]
                        readonly property bool current: backend.page === index
                        readonly property bool sounding: backend.pagesPlaying[index] === true
                        readonly property string silenced: backend.pagesSilenced[index] || ""
                        // The tutorial's next page, while the player isn't on it.
                        readonly property bool called: backend.tutorialActive && backend.tutorialPage === index
                                                       && !current
                        Layout.fillWidth: true
                        Layout.preferredHeight: 58
                        Layout.minimumWidth: 92
                        Rectangle {
                            anchors.fill: parent
                            color: tab.current ? tab.tint : "transparent"
                            border.color: tab.tint
                            border.width: 2
                        }
                        Rectangle {
                            visible: tab.called
                            anchors.fill: parent
                            anchors.margins: -5
                            color: "transparent"
                            border.color: "#FFFFFF"
                            border.width: 3
                            SequentialAnimation on opacity {
                                running: tab.called
                                loops: Animation.Infinite
                                NumberAnimation { to: 0.15; duration: 450 }
                                NumberAnimation { to: 1; duration: 450 }
                            }
                        }
                        Text {
                            x: 8
                            y: 5
                            width: parent.width - 16
                            text: win.pageKey(tab.index) + " " + backend.pageNames[tab.index]
                            color: tab.current ? "#0a0806" : tab.tint
                            font.family: win.mono
                            font.pixelSize: 15
                            font.weight: Font.Bold
                            font.capitalization: Font.AllUppercase
                            elide: Text.ElideRight
                        }
                        Text {
                            x: 8
                            anchors.bottom: parent.bottom
                            anchors.bottomMargin: 6
                            width: parent.width - 30
                            text: tab.silenced !== "" ? "■ " + tab.silenced : backend.pageBlurbs[tab.index]
                            color: tab.current ? "#0a0806" : tab.tint
                            opacity: 0.75
                            font.family: win.mono
                            font.pixelSize: 10
                            font.capitalization: Font.AllUppercase
                            elide: Text.ElideRight
                        }
                        // Lit while one of the part's clips is playing.
                        Rectangle {
                            visible: tab.sounding
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 7
                            width: 10
                            height: 10
                            color: tab.current ? "#0a0806" : tab.tint
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: backend.page = tab.index
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 16

                Surface {
                    id: surface
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    app: backend
                }

                // The right-hand column: the clock and key every part shares,
                // what's ready, the readouts, and the buttons.
                ColumnLayout {
                    Layout.preferredWidth: backend.tutorialActive ? 360 : 300
                    Layout.maximumWidth: backend.tutorialActive ? 360 : 300
                    Layout.fillHeight: true
                    spacing: 12

                    PanelBox {
                        Layout.fillWidth: true
                        Layout.preferredHeight: backend.tutorialActive ? 108 : 128
                        title: qsTr("Transport")
                        color: win.amber
                        Column {
                            spacing: 2
                            Text {
                                text: qsTr("%1 BPM").arg(backend.bpm.toFixed(0)) + "   " + backend.position
                                color: win.amber
                                font.family: win.mono
                                font.pixelSize: 26
                                font.weight: Font.Bold
                            }
                            Text {
                                text: qsTr("Key %1 %2").arg(backend.key).arg(backend.mode)
                                color: win.cyan
                                font.family: win.mono
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                                font.capitalization: Font.AllUppercase
                            }
                            Text {
                                text: qsTr("Chord %1").arg(backend.chord)
                                color: win.cyan
                                font.family: win.mono
                                font.pixelSize: 14
                                font.capitalization: Font.AllUppercase
                            }
                        }
                    }

                    TutorialPanel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 240
                        visible: backend.tutorialActive
                        tint: backend.pageColors[backend.tutorialPage]
                        onPage: backend.page === backend.tutorialPage
                        pageKey: win.pageKey(backend.tutorialPage)
                        pageName: backend.pageNames[backend.tutorialPage]
                        onBack: backend.tutorialBack()
                        onNext: backend.tutorialNext()
                        onLeave: backend.toggleTutorial()
                    }

                    Column {
                        Layout.fillWidth: true
                        visible: !backend.tutorialActive
                        spacing: 6
                        StatusLamp { label: qsTr("Trackpad grabbed"); on: win.playing; color: win.green }
                        StatusLamp { label: qsTr("Audio running"); on: backend.audioOk; color: win.green }
                        StatusLamp { label: qsTr("Metronome"); on: backend.metronome; color: win.amber }
                        StatusLamp { label: qsTr("Recording"); on: backend.recording; color: win.red }
                    }

                    Column {
                        Layout.fillWidth: true
                        visible: !backend.tutorialActive
                        spacing: 0
                        Rectangle {
                            width: parent.width
                            height: 22
                            color: win.green
                            Text {
                                anchors.centerIn: parent
                                text: qsTr("DSP values")
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
                            border.color: win.green
                            border.width: 2
                            Text {
                                anchors.centerIn: parent
                                text: (backend.cpu * 100).toFixed(0) + " %   "
                                      + (backend.latencyMs > 0 ? backend.latencyMs.toFixed(1) + " MS" : "— MS")
                                      + "   " + qsTr("%n voices", "", backend.voices)
                                color: win.green
                                font.family: win.mono
                                font.pixelSize: 17
                                font.weight: Font.DemiBold
                                font.capitalization: Font.AllUppercase
                            }
                        }
                    }

                    GridLayout {
                        id: buttons
                        Layout.fillWidth: true
                        Layout.fillHeight: !backend.tutorialActive
                        Layout.topMargin: 6
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: backend.tutorialActive ? 5 : 8
                        property int bh: backend.tutorialActive ? 34 : 46
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            label: win.playing ? qsTr("Release pad") : qsTr("Take pad")
                            key: win.playing ? "Esc" : ""; color: win.green; active: win.playing
                            hint: backend.tutorialActive && backend.tutorialButton === "pad" && !win.playing
                            onClicked: backend.togglePlay()
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            label: backend.recording
                                   ? qsTr("Rec %1").arg(new Date(backend.recordSeconds * 1000).toISOString().substr(14, 5))
                                   : qsTr("Record")
                            key: "Ctrl R"; color: win.red; active: backend.recording
                            hint: backend.tutorialActive && backend.tutorialButton === "record"
                            onClicked: backend.toggleRecording()
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            visible: !backend.tutorialActive // the keys still work; the tutorial needs the room
                            label: qsTr("Tempo −"); key: "Ctrl ↓"; color: win.amber
                            onClicked: backend.shiftTempo(-1)
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            visible: !backend.tutorialActive // the keys still work; the tutorial needs the room
                            label: qsTr("Tempo +"); key: "Ctrl ↑"; color: win.amber
                            onClicked: backend.shiftTempo(1)
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            visible: !backend.tutorialActive // the keys still work; the tutorial needs the room
                            label: qsTr("Key ◀"); key: "Ctrl ←"; color: win.cyan
                            onClicked: backend.shiftRoot(-1)
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            visible: !backend.tutorialActive // the keys still work; the tutorial needs the room
                            label: qsTr("Key ▶"); key: "Ctrl →"; color: win.cyan
                            onClicked: backend.shiftRoot(1)
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            visible: !backend.tutorialActive // the keys still work; the tutorial needs the room
                            label: qsTr("Click"); key: "Ctrl M"; color: win.amber; active: backend.metronome
                            onClicked: backend.toggleMetronome()
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            label: qsTr("Undo loop"); key: "Ctrl Z"; color: win.amber
                            hint: backend.tutorialActive && backend.tutorialButton === "undo"
                            onClicked: backend.undoTake()
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            label: qsTr("Start over"); key: "Ctrl N ×2"; color: win.red
                            onClicked: backend.startOver()
                        }
                        PanelButton {
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            label: qsTr("Guide"); key: "?"; color: win.cyan; active: win.helpVisible
                            onClicked: win.helpVisible = !win.helpVisible
                        }
                        PanelButton {
                            Layout.columnSpan: 2
                            Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 30; Layout.maximumHeight: 60; Layout.preferredHeight: buttons.bh
                            label: backend.tutorialActive ? qsTr("Leave tutorial") : qsTr("Tutorial: make a track")
                            key: "Ctrl T"; color: win.green; active: backend.tutorialActive
                            onClicked: backend.toggleTutorial()
                        }
                    }
                }
            }

            // Status in inverse video across the bottom: the pad, then what
            // the showing page says its fingers are doing.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                color: win.pageColor
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 16
                    width: parent.width - 32
                    elide: Text.ElideRight
                    text: (backend.padState === Backend.Playing ? qsTr("Playing")
                          : backend.padState === Backend.Starting ? qsTr("Waiting for the trackpad")
                          : backend.padState === Backend.Failed ? qsTr("Fault — %1").arg(backend.error)
                          : backend.padState === Backend.Ready ? qsTr("Paused, pointer released")
                          : qsTr("Standby: Take pad to play"))
                          + ((backend.pagesSilenced[backend.page] || "") !== ""
                             ? "     " + qsTr("Silent: %1 on the arrange page").arg(backend.pagesSilenced[backend.page]) : "")
                          + (backend.pageStatus !== "" ? "     " + backend.pageStatus : "")
                          + (backend.audioOk || backend.audioError === "" ? "" : "     " + qsTr("No sound: %1").arg(backend.audioError))
                          + (backend.lastRecording !== "" ? "     " + qsTr("Saved %1").arg(backend.lastRecording) : "")
                    color: "#0a0806"
                    font.family: win.mono
                    font.pixelSize: 15
                    font.weight: Font.Bold
                    font.letterSpacing: 1
                    font.capitalization: Font.AllUppercase
                }
            }
        }

        // Not playing yet: a console dialog with the button that takes the pad.
        Rectangle {
            anchors.centerIn: parent
            // The tutorial's own first step says how to take the pad (and the
            // mouse plays meanwhile), so the dialog stays out of its way.
            visible: !win.playing && !win.helpVisible && !backend.tutorialActive
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
                    text: backend.error + " " + qsTr("Take the pad again to retry.")
                    color: win.red
                    font.family: win.mono
                    font.pixelSize: 15
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: backend.padState === Backend.Ready
                          ? qsTr("The trackpad is yours again and the sound keeps playing. Take it back with the button below.")
                          : qsTr("The button below hands the trackpad to tenfold: the pointer stops and every finger plays. Esc gives it back at any time. Meanwhile the mouse plays the pad too.")
                    color: win.amber
                    font.family: win.mono
                    font.pixelSize: 15
                    wrapMode: Text.Wrap
                }
                PanelButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 56
                    label: qsTr("Take the trackpad")
                    color: win.green
                    labelSize: 20
                    onClicked: backend.play()
                }
                PanelButton {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 48
                    label: qsTr("New here? The tutorial builds a whole track with you")
                    key: "Ctrl T"
                    color: win.amber
                    labelSize: 15
                    onClicked: backend.toggleTutorial()
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Every part of a track has its own page: 1 drums, 2 bass, 3 chords, 4 lead, 5 arp, 6 atmosphere, 7 effects. Play, then Space loops what you just played on any page; put the loops together on 0, the arrange page. ? shows what the fingers do on the page you're on.")
                    color: win.cyan
                    font.family: win.mono
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                }
            }
        }

        // A notice from a page or a key, big for a moment.
        Text {
            id: noticeLabel
            anchors.centerIn: parent
            width: parent.width - 80
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: backend.notice
            font.family: win.wide
            font.pixelSize: 64
            font.weight: Font.Black
            font.letterSpacing: 6
            font.capitalization: Font.AllUppercase
            color: win.pageColor
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

        HelpPage {
            anchors.fill: parent
            visible: win.helpVisible
            title: win.pageKey(backend.page) + " " + backend.pageNames[backend.page]
            tint: win.pageColor
            pageHelp: backend.pageHelp
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
