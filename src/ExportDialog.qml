import QtQuick
import QtQuick.Layouts

// Export: the song (or the loops playing) as files for another studio: a
// WAV per part, finer voices where a part has them, the mix, and MIDI, all
// from bar 1 and the same length (export.h). Settings first; Export renders
// on a thread of its own, with its progress here and Cancel.
Rectangle {
    id: dialog
    color: "#000000"
    property color tint: "#2EE6FF"
    readonly property color amber: "#FFB000"
    readonly property color green: "#39FF88"
    readonly property color red: "#FF4136"
    readonly property string mono: "Noto Sans Mono"
    signal closed()

    // The choices, set afresh each time the dialog opens.
    property var parts: []
    property bool song: true
    property int bars: 8
    property bool voices: false
    property bool mix: true
    property bool midi: true
    property bool tails: true
    property bool preFader: false
    property bool master: true
    property string prefix: ""

    function reset() {
        backend.refreshExport()
        parts = backend.exportParts.slice()
        song = backend.songBars > 0
        bars = backend.loopBars
        prefix = backend.defaultExportPrefix()
    }
    onVisibleChanged: if (visible && !backend.exporting) reset()
    Component.onCompleted: if (visible) reset()

    function toggled(list, name) {
        const i = list.indexOf(name)
        const out = list.slice()
        if (i >= 0)
            out.splice(i, 1)
        else
            out.push(name)
        return out
    }
    function go() {
        backend.startExport({
            folder: backend.exportFolder, prefix: prefix, song: song, bars: bars, parts: parts,
            voices: voices, mix: mix, midi: midi, tails: tails, preFader: preFader, master: master
        })
    }

    // A click outside the box does nothing (Esc and Close close it).
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }

    component Label: Text {
        color: dialog.amber
        font.family: dialog.mono
        font.pixelSize: 13
        font.weight: Font.DemiBold
        font.letterSpacing: 1.5
        font.capitalization: Font.AllUppercase
    }
    component Toggle: PanelButton {
        Layout.fillWidth: true
        Layout.preferredHeight: 44
        labelSize: 14
    }

    PanelBox {
        id: box
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -37
        width: Math.min(parent.width - 32, 860)
        height: Math.min(parent.height - 32 - 74, column.implicitHeight + 72)
        title: qsTr("Export")
        color: dialog.tint

        Flickable {
            anchors.fill: parent
            anchors.margins: 18
            anchors.topMargin: 24
            contentHeight: column.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: column
                width: parent.width
                spacing: 12
                enabled: !backend.exporting
                opacity: backend.exporting ? 0.55 : 1

                // Where to.
                Label { text: qsTr("Folder") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 44
                        color: "transparent"
                        border.color: dialog.tint
                        border.width: 1
                        Text {
                            anchors.fill: parent
                            anchors.margins: 10
                            verticalAlignment: Text.AlignVCenter
                            text: backend.exportFolder
                            color: dialog.tint
                            elide: Text.ElideMiddle
                            font.family: dialog.mono
                            font.pixelSize: 15
                        }
                    }
                    PanelButton {
                        Layout.preferredWidth: 150
                        Layout.preferredHeight: 44
                        label: qsTr("Choose…")
                        color: dialog.tint
                        onClicked: backend.pickExportFolder()
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Label { text: qsTr("Names start with") }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 36
                        color: "transparent"
                        border.color: prefixInput.activeFocus ? dialog.amber : Qt.darker(dialog.amber, 1.8)
                        border.width: 1
                        TextInput {
                            id: prefixInput
                            anchors.fill: parent
                            anchors.margins: 8
                            verticalAlignment: TextInput.AlignVCenter
                            text: dialog.prefix
                            onTextEdited: dialog.prefix = text
                            color: dialog.amber
                            selectionColor: dialog.amber
                            selectedTextColor: "#0a0806"
                            font.family: dialog.mono
                            font.pixelSize: 15
                            clip: true
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("e.g. %1").arg(dialog.prefix + "01 Drums - " + Math.round(backend.bpm) + " bpm "
                                                + backend.key + " " + backend.mode.toLowerCase() + ".wav")
                    color: Qt.darker(dialog.amber, 1.4)
                    elide: Text.ElideMiddle
                    font.family: dialog.mono
                    font.pixelSize: 12
                }

                // What plays.
                Label { text: qsTr("Length") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Toggle {
                        label: backend.songBars > 0 ? qsTr("Song · %1 bars").arg(backend.songBars) : qsTr("Song · none yet")
                        color: dialog.green
                        active: dialog.song && backend.songBars > 0
                        opacity: backend.songBars > 0 ? 1 : 0.4
                        onClicked: if (backend.songBars > 0) dialog.song = true
                    }
                    Toggle {
                        label: qsTr("Loops playing · %1 bars").arg(dialog.bars)
                        color: dialog.green
                        active: !dialog.song || backend.songBars === 0
                        onClicked: dialog.song = false
                    }
                    PanelButton {
                        Layout.preferredWidth: 56; Layout.preferredHeight: 44
                        label: "−"; color: dialog.green
                        onClicked: { dialog.song = false; dialog.bars = Math.max(1, dialog.bars - (dialog.bars > 4 ? 4 : 1)) }
                    }
                    PanelButton {
                        Layout.preferredWidth: 56; Layout.preferredHeight: 44
                        label: "+"; color: dialog.green
                        onClicked: { dialog.song = false; dialog.bars = Math.min(512, dialog.bars + (dialog.bars >= 4 ? 4 : 1)) }
                    }
                }

                // Which parts.
                Label { text: qsTr("Parts · a WAV each") }
                GridLayout {
                    Layout.fillWidth: true
                    columns: Math.max(2, Math.min(5, Math.floor(column.width / 160)))
                    columnSpacing: 8
                    rowSpacing: 8
                    Repeater {
                        model: backend.exportParts
                        Toggle {
                            required property string modelData
                            readonly property int index: backend.pageNames.indexOf(modelData)
                            label: modelData
                            color: index >= 0 ? backend.pageColors[index] : dialog.amber
                            active: dialog.parts.indexOf(modelData) >= 0
                            onClicked: dialog.parts = dialog.toggled(dialog.parts, modelData)
                        }
                    }
                    Text {
                        visible: backend.exportParts.length === 0
                        text: qsTr("No loops yet")
                        color: dialog.red
                        font.family: dialog.mono
                        font.pixelSize: 14
                    }
                }

                // How.
                Label { text: qsTr("Files") }
                GridLayout {
                    Layout.fillWidth: true
                    columns: Math.max(2, Math.min(3, Math.floor(column.width / 240)))
                    columnSpacing: 8
                    rowSpacing: 8
                    Toggle {
                        label: qsTr("Individual voices"); key: qsTr("pads · stems · layers")
                        color: dialog.amber; active: dialog.voices
                        onClicked: dialog.voices = !dialog.voices
                    }
                    Toggle {
                        label: qsTr("Full mix"); key: qsTr("as heard")
                        color: dialog.amber; active: dialog.mix
                        onClicked: dialog.mix = !dialog.mix
                    }
                    Toggle {
                        label: qsTr("MIDI file"); key: qsTr("drums bass chords lead arp")
                        color: dialog.amber; active: dialog.midi
                        onClicked: dialog.midi = !dialog.midi
                    }
                    Toggle {
                        label: qsTr("Tails"); key: dialog.tails ? qsTr("ring out") : qsTr("cut at the end")
                        color: dialog.amber; active: dialog.tails
                        onClicked: dialog.tails = !dialog.tails
                    }
                    Toggle {
                        label: dialog.preFader ? qsTr("Pre-fader") : qsTr("Post-fader")
                        key: dialog.preFader ? qsTr("dry, no room") : qsTr("mix, room, sends")
                        color: dialog.amber; active: !dialog.preFader
                        onClicked: dialog.preFader = !dialog.preFader
                    }
                    Toggle {
                        label: qsTr("Master glue")
                        key: dialog.preFader ? "—" : dialog.master ? qsTr("stems add up to the mix") : qsTr("clean, headroom")
                        color: dialog.amber; active: dialog.master && !dialog.preFader
                        opacity: dialog.preFader ? 0.4 : 1
                        onClicked: if (!dialog.preFader) dialog.master = !dialog.master
                    }
                }
                Text {
                    Layout.fillWidth: true
                    Layout.bottomMargin: 6
                    elide: Text.ElideRight
                    lineHeight: 1.2
                    text: qsTr("Every file from beat 1 of bar 1, %1 Hz 24-bit, all one length; tempo and beats inside (ACID).")
                          .arg(backend.sampleRate) + "\n"
                          + qsTr("In Live: set the tempo to %1 and drop them all at 1.1.1.").arg(Math.round(backend.bpm))
                    color: Qt.darker(dialog.tint, 1.3)
                    font.family: dialog.mono
                    font.pixelSize: 12
                }
            }
        }
    }

    // The progress and the buttons, under the box's contents.
    PanelBox {
        anchors.top: box.bottom
        anchors.topMargin: 10
        anchors.horizontalCenter: box.horizontalCenter
        width: box.width
        height: 64
        color: backend.exporting ? dialog.green : dialog.tint
        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Rectangle {
                    visible: backend.exporting
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    height: 6
                    width: parent.width * backend.exportProgress
                    color: dialog.green
                }
                Text {
                    anchors.fill: parent
                    anchors.bottomMargin: backend.exporting ? 12 : 0
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideMiddle
                    text: backend.exporting ? backend.exportStatus + "  " + Math.round(backend.exportProgress * 100) + "%"
                                            : backend.exportStatus
                    color: backend.exporting ? dialog.green : dialog.tint
                    font.family: dialog.mono
                    font.pixelSize: 13
                }
            }
            PanelButton {
                Layout.preferredWidth: 120
                Layout.fillHeight: true
                visible: !backend.exporting && backend.exportedFolder !== ""
                label: qsTr("Open folder"); color: dialog.tint
                onClicked: Qt.openUrlExternally("file://" + backend.exportedFolder)
            }
            PanelButton {
                Layout.preferredWidth: 120
                Layout.fillHeight: true
                label: backend.exporting ? qsTr("Cancel") : qsTr("Close")
                key: "Esc"
                color: dialog.red
                onClicked: backend.exporting ? backend.cancelExport() : dialog.closed()
            }
            PanelButton {
                Layout.preferredWidth: 150
                Layout.fillHeight: true
                visible: !backend.exporting
                label: qsTr("Export")
                color: dialog.green
                active: true
                opacity: dialog.parts.length > 0 || dialog.mix ? 1 : 0.4
                onClicked: if (dialog.parts.length > 0 || dialog.mix) dialog.go()
            }
        }
    }
}
