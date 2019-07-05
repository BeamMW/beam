import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Window 2.2
import "."

Label {
    id: control
    property bool copyMenuEnabled: false
    property bool enableBackgroundRect: true
    property color selectionColor: control.palette.highlight
    property color selectedTextColor: control.palette.highlightedText
    property color textColor: control.palette.text

    signal copyText()

	font { 
		family: "SF Pro Display"
		styleName: "Regular"
		weight: Font.Normal
	}

    background: Rectangle {
	    id: backgroundRect
        visible: enableBackgroundRect & contextMenu.opened
        anchors.left: control.left
        anchors.top: control.top
        height: control.height
        width: control.contentWidth
        color: selectionColor
    }

    MouseArea {
        enabled: control.copyMenuEnabled
        anchors.left: control.left
        anchors.top: control.top
        height: control.height
        width: control.contentWidth
        acceptedButtons: Qt.RightButton
        hoverEnabled: true

        onClicked: {
            contextMenu.x = mouse.x
            contextMenu.y = mouse.y
            contextMenu.open()
        }
    }

    ContextMenu {
        id: contextMenu
        modal: true
        dim: false
        enabled: control.copyMenuEnabled
        Action {
            //% "Copy"
            text: qsTrId("general-copy")
            icon.source: "qrc:/assets/icon-copy.svg"
            enabled: control.enabled && control.copyMenuEnabled
            onTriggered: control.copyText()
        }
        onOpened: {
            control.color = control.selectedTextColor;
        }
        onClosed: {
            control.color = control.textColor;
        }
    }

    Component.onCompleted: {
        control.textColor = control.color;
    }
}
