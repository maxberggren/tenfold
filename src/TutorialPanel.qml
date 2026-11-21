import QtQuick
import QtQuick.Layouts

// The tutorial's panel: where the song is up to, the step's instruction,
// its checks ticking off as the player does them, the keys it uses, and
// Back, Next and Leave. A step done turns the panel green for a moment.
Item {
    id: panel
    property color tint: "#FFB000"     // the step's page colour
    property bool onPage: true          // the player is on the step's page
    property string pageKey: ""         // the number that takes them there
    property string pageName: ""
    readonly property color amber: "#FFB000"
    readonly property color green: "#39FF88"
    readonly property color paper: "#F4E9D4"
    readonly property string mono: "Noto Sans Mono"
    readonly property int state: backend.tutorialState
    readonly property bool last: backend.tutorialStep === backend.tutorialCount - 1

    signal back()
    signal next()
    signal leave()

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        border.color: panel.state === 1 ? panel.green : panel.amber
        border.width: 3
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 3
        spacing: 0

        // The header: tutorial, step of steps, the chapter.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 30
            color: panel.state === 1 ? panel.green : panel.amber
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 10
                text: qsTr("Tutorial  %1 / %2").arg(backend.tutorialStep + 1).arg(backend.tutorialCount)
                color: "#0a0806"
                font.family: panel.mono
                font.pixelSize: 15
                font.weight: Font.Bold
                font.letterSpacing: 1.5
                font.capitalization: Font.AllUppercase
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                anchors.rightMargin: 10
                text: backend.tutorialChapters[backend.tutorialChapter] || ""
                color: "#0a0806"
                font.family: panel.mono
                font.pixelSize: 13
                font.weight: Font.Bold
                font.letterSpacing: 1.5
                font.capitalization: Font.AllUppercase
            }
        }

        // The song so far: one block per chapter, lit up to the one playing.
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 8
            spacing: 3
            Repeater {
                model: backend.tutorialChapters
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    readonly property color c: backend.tutorialChapterColors[index]
                    Layout.fillWidth: true
                    Layout.preferredHeight: 18
                    color: index < backend.tutorialChapter ? c : "transparent"
                    border.color: c
                    border.width: index === backend.tutorialChapter ? 2 : 1
                    opacity: index <= backend.tutorialChapter ? 1 : 0.45
                    Rectangle {
                        visible: index === backend.tutorialChapter
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: parent.width * 0.5
                        color: c
                        opacity: 0.6
                    }
                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        color: index < backend.tutorialChapter ? "#0a0806" : c
                        font.family: panel.mono
                        font.pixelSize: 9
                        font.weight: Font.Bold
                        font.capitalization: Font.AllUppercase
                    }
                }
            }
        }

        // The step itself; a long one scrolls rather than pushing the
        // buttons off the panel.
        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: body.implicitHeight + 10
            boundsBehavior: Flickable.StopAtBounds
            ColumnLayout {
                id: body
                width: parent.width
                spacing: 0
                // Not on the step's page: which number goes there.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    Layout.topMargin: 10
                    Layout.preferredHeight: 40
                    visible: !panel.onPage
                    color: panel.tint
                    SequentialAnimation on opacity {
                        running: !panel.onPage
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.55; duration: 450 }
                        NumberAnimation { to: 1; duration: 450 }
                    }
                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Press %1 · %2").arg(panel.pageKey).arg(panel.pageName)
                        color: "#0a0806"
                        font.family: panel.mono
                        font.pixelSize: 20
                        font.weight: Font.Black
                        font.letterSpacing: 2
                        font.capitalization: Font.AllUppercase
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.topMargin: 10
                    text: backend.tutorialTitle
                    color: panel.tint
                    font.family: panel.mono
                    font.pixelSize: 21
                    font.weight: Font.Black
                    font.capitalization: Font.AllUppercase
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.topMargin: 6
                    textFormat: Text.StyledText
                    text: backend.rich(backend.tutorialText, panel.paper, font.pixelSize)
                    color: panel.paper
                    font.family: panel.mono
                    // A small window keeps the whole instruction in view.
                    font.pixelSize: panel.height < 380 ? 13 : 14
                    lineHeight: panel.height < 380 ? 1.04 : 1.12
                    wrapMode: Text.Wrap
                }

                // The keys the step uses, as keycaps.
                Flow {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.topMargin: 10
                    spacing: 6
                    visible: backend.tutorialKeys.length > 0
                    Repeater {
                        model: backend.tutorialKeys
                        delegate: Rectangle {
                            required property string modelData
                            width: Math.max(34, keyText.implicitWidth + 16)
                            height: 30
                            color: "transparent"
                            border.color: panel.amber
                            border.width: 2
                            radius: 3
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: 4
                                anchors.bottomMargin: 6
                                color: "transparent"
                                border.color: Qt.darker(panel.amber, 2.2)
                                border.width: 1
                                radius: 2
                            }
                            Text {
                                id: keyText
                                anchors.centerIn: parent
                                anchors.verticalCenterOffset: -1
                                textFormat: Text.StyledText
                                text: backend.rich(modelData, panel.amber, 13)
                                color: panel.amber
                                font.family: panel.mono
                                font.pixelSize: 13
                                font.weight: Font.Bold
                                font.capitalization: Font.AllUppercase
                            }
                        }
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.topMargin: 10
                    visible: backend.tutorialTip !== ""
                    text: "<b>" + qsTr("Try") + "</b>  " + backend.rich(backend.tutorialTip, "#2EE6FF", 13)
                    textFormat: Text.StyledText
                    color: "#2EE6FF"
                    font.family: panel.mono
                    font.pixelSize: 13
                    wrapMode: Text.Wrap
                }

                // At the end, where the track went.
                Text {
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.topMargin: 10
                    visible: panel.last && backend.lastRecording !== ""
                    text: qsTr("Saved %1").arg(backend.lastRecording)
                    color: panel.green
                    font.family: panel.mono
                    font.pixelSize: 13
                    wrapMode: Text.WrapAnywhere
                }
            }
            // More below: a fade and an arrow, while there is.
            Rectangle {
                visible: flick.contentY + flick.height < flick.contentHeight - 4
                y: flick.contentY + flick.height - height
                width: flick.width
                height: 26
                gradient: Gradient {
                    GradientStop { position: 0; color: "#00000000" }
                    GradientStop { position: 1; color: "#000000" }
                }
                Text {
                    anchors.right: parent.right
                    anchors.rightMargin: 14
                    anchors.bottom: parent.bottom
                    textFormat: Text.StyledText
                    text: backend.rich("▼ " + qsTr("more"), panel.amber, 11)
                    color: panel.amber
                    font.family: panel.mono
                    font.pixelSize: 11
                    font.capitalization: Font.AllUppercase
                }
            }
        }

        // The checks, ticking off, side by side where they fit: always in view,
        // under the instruction however long it is.
        Flow {
            Layout.fillWidth: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.topMargin: 8
            spacing: 12
            Repeater {
                model: backend.tutorialChecks
                delegate: Row {
                    required property var modelData
                    spacing: 8
                    Rectangle {
                        width: 16
                        height: 16
                        anchors.verticalCenter: parent.verticalCenter
                        color: modelData.done ? panel.green : "transparent"
                        border.color: modelData.done ? panel.green : panel.tint
                        border.width: 2
                        Text {
                            anchors.centerIn: parent
                            visible: modelData.done
                            text: "✓"
                            color: "#0a0806"
                            font.pixelSize: 13
                            font.weight: Font.Black
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        color: modelData.done ? panel.green : panel.tint
                        font.family: panel.mono
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        font.capitalization: Font.MixedCase
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 8
            PanelButton {
                Layout.fillWidth: true
                Layout.preferredHeight: panel.height < 380 ? 34 : 44
                label: qsTr("Back")
                key: "Ctrl ⇧ ↵"
                color: panel.amber
                labelSize: 14
                opacity: backend.tutorialStep > 0 ? 1 : 0.35
                onClicked: panel.back()
            }
            PanelButton {
                Layout.fillWidth: true
                Layout.preferredHeight: panel.height < 380 ? 34 : 44
                label: panel.last ? qsTr("Finish") : panel.state === 0 ? qsTr("Skip") : qsTr("Next")
                key: "Ctrl ↵"
                color: panel.state === 0 ? panel.amber : panel.green
                active: panel.state === 2
                labelSize: 14
                onClicked: panel.next()
            }
            PanelButton {
                Layout.fillWidth: true
                Layout.preferredHeight: panel.height < 380 ? 34 : 44
                label: qsTr("Leave")
                key: "Esc"
                color: "#FF4136"
                labelSize: 14
                onClicked: panel.leave()
            }
        }
    }

    // Done: the cheer, big, over the panel's lower half for a moment.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 3
        height: cheerText.implicitHeight + 34
        visible: panel.state === 1
        color: panel.green
        Text {
            id: cheerText
            anchors.centerIn: parent
            width: parent.width - 28
            horizontalAlignment: Text.AlignHCenter
            text: "✓  " + backend.tutorialCheer
            color: "#0a0806"
            font.family: panel.mono
            font.pixelSize: 20
            font.weight: Font.Black
            font.capitalization: Font.AllUppercase
            wrapMode: Text.Wrap
        }
    }
}
