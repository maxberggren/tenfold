import QtQuick
import QtQuick.Layouts

// Everything the pad and the keyboard do, as a console page.
Rectangle {
    color: "#000000"

    MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

    Flickable {
        anchors.fill: parent
        anchors.margins: 30
        contentHeight: content.implicitHeight
        clip: true

        ColumnLayout {
            id: content
            width: parent.width
            spacing: 18

            Rectangle {
                Layout.fillWidth: true
                height: 34
                color: win.cyan
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    x: 14
                    text: qsTr("Gestures — operator reference") + "      " + qsTr("Esc or ? to close")
                    color: "#0a0806"
                    font.family: win.mono
                    font.pixelSize: 17
                    font.weight: Font.Bold
                    font.letterSpacing: 2
                    font.capitalization: Font.AllUppercase
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 24

                GridLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 16

                    Repeater {
                        model: [
                            [qsTr("Rows"), win.orange, qsTr("Five instruments, bottom to top: drums, bass, chords, lead, arp. On the drum row the column picks the piece: kick, snare, clap, hat, open hat, perc. Above it, left to right is the scale, so every finger lands in key. A finger plays the moment it lands; how hard you land is how loud.")],
                            [qsTr("Hold"), win.amber, qsTr("A held finger sustains its note (a held drum rolls) and takes the next free knob, in the order your fingers arrived: 1 Filter, 2 Env, 3 Width, 4 Drive, 5 Pump, 6 Echo, 7 Space, 8 Glide, 9 Wobble, 10 Gravity. Slide it up or down for the first value, twist for the second. Knobs stay where you leave them.")],
                            [qsTr("Chords"), win.cyan, qsTr("One finger on the chord row plays the seventh chord on that note of the scale. Slide it sideways to walk a progression; the chords are voiced close together. The arp row plays the same chord as an arpeggio, in time.")],
                            [qsTr("Loop"), win.magenta, qsTr("Tap a rhythm, then land on the next beat and stay. The taps become a loop, tightened to the grid, and your first loop sets the tempo. While you hold: slide sideways to transpose, up or down for wetness, twist right for rolls, left to thin it out, press harder to accent.")],
                            [qsTr("Record motion"), win.magenta, qsTr("While you hold a loop, hold other fingers too: their notes, pressure and knob sweeps are recorded into the bar and replay every time round.")],
                            [qsTr("Pump"), win.magenta, qsTr("Every kick ducks the rest of the mix and lets it swell back. Loops repeat until dissolved; tap once and hold on a loop's dial to pick it up again.")],
                            [qsTr("Press"), win.green, qsTr("Click the pad to latch what you're holding: held notes keep sounding with the sound they had, and a held loop is pinned. Fingers that latched a chord keep their knobs while they stay down, so the next finger reaches the next knob.")],
                            [qsTr("Layers"), win.green, qsTr("Tap once and hold on a latched chord's lettered box to pick it up: slide sideways to move it through the scale, up or down for its filter, twist to widen it, press to swell it. Press the pad to pin it.")],
                            [qsTr("Swipe"), win.cyan, qsTr("Three fingers flicked sideways move the key a fifth; up or down, the octave. Four fingers sideways change the scale family.")],
                            [qsTr("Palm"), win.red, qsTr("Rest a palm on the pad to damp everything, like a hand on strings. Palm and press clears the landscape.")]
                        ]
                        delegate: PanelBox {
                            Layout.fillWidth: true
                            Layout.preferredHeight: body.implicitHeight + 46
                            title: modelData[0]
                            color: modelData[1]
                            Text {
                                id: body
                                width: parent.width
                                text: modelData[2]
                                color: modelData[1]
                                wrapMode: Text.Wrap
                                font.family: win.mono
                                font.pixelSize: 14
                            }
                        }
                    }
                }

                PanelBox {
                    Layout.preferredWidth: 410
                    Layout.preferredHeight: keys.implicitHeight + 50
                    Layout.alignment: Qt.AlignTop
                    title: qsTr("Keys")
                    color: win.amber
                    Column {
                        id: keys
                        width: parent.width
                        spacing: 8
                        Repeater {
                            model: [
                                ["Space", qsTr("Take the trackpad / give it back")],
                                ["Esc", qsTr("Give the trackpad back")],
                                ["← →", qsTr("Key down or up a fifth")],
                                ["↑ ↓", qsTr("Scale family")],
                                ["[ ]", qsTr("Octave down or up")],
                                ["J", qsTr("Just or equal tuning")],
                                ["⌫", qsTr("Dissolve the newest layer")],
                                ["⇧ ⌫", qsTr("Dissolve everything")],
                                ["0", qsTr("Knobs back to the start")],
                                ["R", qsTr("Record to ~/Music/tenfold")],
                                ["F", qsTr("Full screen")],
                                ["T", qsTr("Tutorial on or off")],
                        ["N  B", qsTr("Tutorial: next or back")],
                        ["?", qsTr("This page")],
                                ["Q", qsTr("Quit")]
                            ]
                            delegate: Row {
                                spacing: 12
                                Rectangle {
                                    width: 86
                                    height: 26
                                    color: win.amber
                                    Text {
                                        anchors.centerIn: parent
                                        text: modelData[0]
                                        color: "#0a0806"
                                        font.family: win.mono
                                        font.pixelSize: 14
                                        font.weight: Font.Bold
                                    }
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: modelData[1]
                                    color: win.amber
                                    font.family: win.mono
                                    font.pixelSize: 14
                                    font.capitalization: Font.AllUppercase
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
