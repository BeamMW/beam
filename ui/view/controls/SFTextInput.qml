import QtQuick 2.11
import QtQuick.Window 2.2

TextInput {
    FontLoader { id: sf_pro_display; source: "qrc:///assets/fonts/SF-Pro-Display-Regular.otf"; }
    FontLoader { source: "qrc:///assets/fonts/SF-Pro-Display-Bold.otf"; }
    FontLoader { source: "qrc:///assets/fonts/SF-Pro-Display-Thin.otf"; }

    font {
        family: sf_pro_display.name
    }
    activeFocusOnTab: true
    selectByMouse: true

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.IBeamCursor
        acceptedButtons: Qt.NoButton
    }
}
