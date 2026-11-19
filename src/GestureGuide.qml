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
                            [qsTr("Land"), win.orange, qsTr("Every finger strikes where it lands. Left to right is the note, in the current scale. Bottom to top is the material: skin, snare, wood, glass, air. How hard you land is how hard it's hit; a fingertip is a hard stick, a flat finger a soft mallet.")],
                            [qsTr("Hold"), win.amber, qsTr("A finger that stays blooms into a voice and takes the next free role, in the order your fingers arrived: 1 Tone, 2 Shadow, 3 Air, 4 Space, 5 Pulse, 6 Echo, 7 Grit, 8 Drift, 9 Swarm, 10 Gravity. Slide it up or down to turn the role's first knob, twist it for the second. Knobs stay where you leave them.")],
                            [qsTr("Glide"), win.amber, qsTr("Move a held finger slowly and it settles on the nearest note; slide it quickly and it glides. A small rock of the finger is vibrato. Lean in to open the sound.")],
                            [qsTr("Loop"), win.magenta, qsTr("Tap a rhythm, then land on the next beat and stay. The taps become a loop, and your first loop sets the tempo. While you hold: slide sideways to transpose, up or down for material and wetness, twist right for rolls and ghost notes, left to thin it out, press harder to accent.")],
                            [qsTr("Record motion"), win.magenta, qsTr("While you hold a loop, hold other fingers too: their notes, pressure and knob sweeps are recorded into the bar and replay every time round.")],
                            [qsTr("Erode"), win.magenta, qsTr("A loop you let go of keeps playing and slowly wears away like old tape. Tap once and hold on its ring to pick it up again.")],
                            [qsTr("Press"), win.green, qsTr("Click the pad to keep what you're holding: held voices freeze into the landscape with their own timbre, and a held loop is pinned. Fingers that froze a chord keep their roles while they stay down, so the next finger reaches the next knob.")],
                            [qsTr("Layers"), win.green, qsTr("Tap once and hold on a frozen chord's lettered ring to pick it up: slide sideways to transpose it, up or down to brighten it, twist to widen it, press to swell it. Press the pad to pin it.")],
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
