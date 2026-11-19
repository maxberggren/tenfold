import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Everything the pad and the keyboard do, on one page.
Rectangle {
    color: "#f00b0b0e"

    MouseArea { anchors.fill: parent; onClicked: win.helpVisible = false }

    Flickable {
        anchors.fill: parent
        anchors.margins: 36
        contentHeight: content.implicitHeight
        clip: true

        RowLayout {
            id: content
            width: parent.width
            spacing: 48

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: 14

                Label { text: qsTr("The pad"); font.pixelSize: 22; font.weight: Font.DemiBold; color: theme.accent }
                Repeater {
                    model: [
                        [qsTr("Land"), qsTr("Every finger strikes where it lands. Left to right is the note, in the current scale. Bottom to top is the material: skin, wood, glass, air. How hard you land is how hard it's hit.")],
                        [qsTr("Hold"), qsTr("A finger that stays blooms into a voice and takes the next free role, in the order your fingers arrived: 1 Tone, 2 Shadow, 3 Air, 4 Space, 5 Pulse, 6 Echo, 7 Grit, 8 Drift, 9 Swarm, 10 Gravity. Slide it up or down to turn the role's first knob, twist it for the second. Knobs stay where you leave them.")],
                        [qsTr("Glide"), qsTr("Move a held finger slowly and it settles on the nearest note; slide it quickly and it glides. A small rock of the finger is vibrato.")],
                        [qsTr("Loop"), qsTr("Tap a rhythm, then land on the next beat and stay. The taps become a loop, and your first loop sets the tempo. While you hold: slide sideways to transpose it, up or down to change its material, twist right for rolls and ghost notes, left to thin it out, press harder to accent. Taps with other fingers are added to it.")],
                        [qsTr("Erode"), qsTr("A loop you let go of keeps playing and slowly wears away like old tape. Tap once and hold on its ring to pick it up again.")],
                        [qsTr("Press"), qsTr("Click the pad to keep what you're holding: held voices freeze into the landscape and your fingers are free; a held loop is pinned and stops eroding.")],
                        [qsTr("Palm"), qsTr("Rest a palm on the pad to damp everything, like a hand on strings. Palm and press clears the landscape.")]
                    ]
                    delegate: ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { text: modelData[0]; font.pixelSize: 15; font.weight: Font.DemiBold; color: "#f2f2f2" }
                        Label { Layout.fillWidth: true; text: modelData[1]; wrapMode: Text.Wrap; color: "#b8b8c0" }
                    }
                }
            }

            ColumnLayout {
                Layout.preferredWidth: 360
                Layout.alignment: Qt.AlignTop
                spacing: 8

                Label { text: qsTr("Keys"); font.pixelSize: 22; font.weight: Font.DemiBold; color: theme.accent }
                Repeater {
                    model: [
                        ["Space", qsTr("Take the trackpad / give it back")],
                        ["Esc", qsTr("Give the trackpad back")],
                        ["← →", qsTr("Key down or up a fifth")],
                        ["↑ ↓", qsTr("Scale family")],
                        ["J", qsTr("Just intonation or equal temperament")],
                        ["Backspace", qsTr("Dissolve the newest layer")],
                        ["Shift+Backspace", qsTr("Dissolve everything")],
                        ["0", qsTr("Knobs back to where they started")],
                        ["R", qsTr("Record to ~/Music/tenfold")],
                        ["F", qsTr("Full screen")],
                        ["?", qsTr("This page")],
                        ["Q", qsTr("Quit")]
                    ]
                    delegate: RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Rectangle {
                            implicitWidth: 120
                            implicitHeight: 26
                            radius: Math.min(4, theme.radius)
                            color: Qt.rgba(1, 1, 1, 0.07)
                            Label { anchors.centerIn: parent; text: modelData[0]; color: "#f2f2f2"; font.pixelSize: 13 }
                        }
                        Label { Layout.fillWidth: true; text: modelData[1]; color: "#b8b8c0"; wrapMode: Text.Wrap }
                    }
                }
            }
        }
    }
}
