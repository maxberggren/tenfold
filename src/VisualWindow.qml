import QtQuick
import QtQuick.Window

// The picture in a window of its own, for a projector or a second screen:
// Return on the Visual page opens and closes it, Shift+Return (or F11, or
// a double-click here) puts it full screen, on the other screen when there
// is one. Esc leaves full screen.
//
// The trackpad keeps playing the main window's page whichever window has
// the focus (the pad is taken while any tenfold window is in front), and
// the keys typed here go to the main window's page too, so a set can be
// played with this window in front.
Window {
    id: w
    width: 1280
    height: 720
    minimumWidth: 320
    minimumHeight: 180
    title: qsTr("tenfold · visual")
    color: "#000000"
    signal back()

    // Full screen, on another screen than the main window's when there is one.
    function toggleFullScreen(other) {
        if (w.visibility === Window.FullScreen) {
            w.showNormal()
            return
        }
        const screens = Qt.application.screens
        if (screens.length > 1 && other) {
            for (let i = 0; i < screens.length; ++i)
                if (screens[i].name !== other.name) {
                    w.screen = screens[i]
                    break
                }
        }
        w.showFullScreen()
    }

    Visualizer {
        anchors.fill: parent
        crt: 1.0
    }

    // Keys here play the main window's page, as there.
    Item {
        anchors.fill: parent
        focus: true
        Keys.onPressed: event => {
            event.accepted = backend.sendKey(event.key, event.modifiers, true, event.isAutoRepeat, event.text)
        }
        Keys.onReleased: event => {
            event.accepted = backend.sendKey(event.key, event.modifiers, false, event.isAutoRepeat, event.text)
        }
    }

    Shortcut { sequence: "F11"; context: Qt.WindowShortcut; onActivated: w.toggleFullScreen(null) }
    Shortcut {
        sequence: "Esc"; context: Qt.WindowShortcut
        onActivated: w.visibility === Window.FullScreen ? w.showNormal() : w.back()
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: w.visibility === Window.FullScreen ? Qt.BlankCursor : Qt.ArrowCursor
        onDoubleClicked: w.toggleFullScreen(null)
    }
}
