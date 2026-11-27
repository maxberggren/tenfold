import QtQuick
import QtQuick.Layouts

// The sound browser: every sound the page has, as tiles over the pad. A tile
// clicked is chosen at once: the browser closes and a bar of the page's own
// playing is heard in it. The arrows step through the tiles, hearing each,
// Return keeps the one lit and Esc goes back to the sound it opened with.
Rectangle {
    id: browser
    property color tint: "#FFB000"
    property string pageName: ""
    // The sound the browser opened with, for BACK.
    property int openedWith: -1
    signal closed()

    color: "#000000"
    border.color: tint
    border.width: 2
    focus: visible

    readonly property var names: backend.soundNames
    readonly property var whats: backend.soundWhats
    readonly property int current: backend.soundIndex
    readonly property int count: names.length

    onVisibleChanged: {
        if (visible) {
            openedWith = backend.soundIndex
            forceActiveFocus()
        }
    }

    function keep() { browser.closed() }
    function back() {
        if (openedWith >= 0 && openedWith !== backend.soundIndex)
            backend.chooseSound(openedWith, false)
        browser.closed()
    }
    function step(d) {
        if (count === 0)
            return
        backend.chooseSound(((Math.max(0, current) + d) % count + count) % count, true)
    }

    Keys.onPressed: event => {
        event.accepted = true
        if (event.key === Qt.Key_Left) step(-1)
        else if (event.key === Qt.Key_Right) step(1)
        else if (event.key === Qt.Key_Up) step(-grid.columns)
        else if (event.key === Qt.Key_Down) step(grid.columns)
        else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space) keep()
        else if (event.key === Qt.Key_Escape) back()
        else event.accepted = false
    }

    // The title cut into the top edge, as the pages' own legends are.
    Rectangle {
        x: 16
        y: -titleText.height / 2
        width: titleText.implicitWidth + 16
        height: titleText.height
        color: "#000000"
        Text {
            id: titleText
            x: 8
            text: qsTr("Sounds · %1").arg(browser.pageName)
            color: browser.tint
            font.family: "Noto Sans Mono"
            font.pixelSize: 14
            font.weight: Font.Bold
            font.letterSpacing: 1.5
            font.capitalization: Font.AllUppercase
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        anchors.topMargin: 22
        spacing: 12

        Text {
            Layout.fillWidth: true
            text: qsTr("Click a sound to play with it. Esc keeps the one you had.")
            color: browser.tint
            opacity: 0.8
            font.family: "Noto Sans Mono"
            font.pixelSize: 14
            wrapMode: Text.Wrap
        }

        GridLayout {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            // As many across as keep a tile about 180 px wide, the rows
            // sharing the height.
            columns: Math.max(2, Math.min(6, Math.floor((width + columnSpacing) / (180 + columnSpacing))))
            readonly property int rows: Math.ceil(browser.count / columns)
            columnSpacing: 8
            rowSpacing: 8

            Repeater {
                model: browser.count
                delegate: Item {
                    id: tile
                    required property int index
                    readonly property bool lit: index === browser.current
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.preferredWidth: 180
                    Layout.minimumHeight: 34
                    Layout.maximumHeight: 96

                    Rectangle {
                        anchors.fill: parent
                        color: tile.lit ? browser.tint : tileMouse.containsMouse ? Qt.rgba(browser.tint.r, browser.tint.g, browser.tint.b, 0.14) : "transparent"
                        border.color: browser.tint
                        border.width: tile.lit ? 2 : 1
                        opacity: tile.lit || tileMouse.containsMouse ? 1 : 0.85
                    }
                    // Playing now: a sweep under the name while its phrase sounds.
                    Rectangle {
                        visible: tile.lit && backend.auditioning
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        height: 3
                        color: "#0a0806"
                        SequentialAnimation on width {
                            running: tile.lit && backend.auditioning
                            loops: Animation.Infinite
                            NumberAnimation { from: 0; to: tile.width; duration: 900 }
                        }
                    }
                    Column {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 2
                        Text {
                            width: parent.width
                            text: browser.names[tile.index]
                            color: tile.lit ? "#0a0806" : browser.tint
                            font.family: "Noto Sans Mono"
                            font.pixelSize: tile.height >= 56 ? 17 : 14
                            font.weight: Font.Bold
                            font.capitalization: Font.AllUppercase
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: tile.height >= 52
                            width: parent.width
                            height: tile.height - 8 - 16 - 24
                            text: browser.whats[tile.index] || ""
                            color: tile.lit ? "#0a0806" : browser.tint
                            opacity: 0.75
                            font.family: "Noto Sans Mono"
                            font.pixelSize: 12
                            wrapMode: Text.Wrap
                            elide: Text.ElideRight
                            maximumLineCount: 2
                        }
                    }
                    MouseArea {
                        id: tileMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            backend.chooseSound(tile.index, true)
                            browser.keep()
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Item { Layout.fillWidth: true }
            Text {
                text: qsTr("← → hear the next · Enter keeps it")
                textFormat: Text.StyledText
                color: browser.tint
                opacity: 0.6
                font.family: "Noto Sans Mono"
                font.pixelSize: 12
                font.capitalization: Font.AllUppercase
            }
        }
    }
}
