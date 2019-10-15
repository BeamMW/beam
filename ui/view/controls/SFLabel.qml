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
        visible: false// enableBackgroundRect & contextMenu.opened
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
            var contextMenu = contextMenuComponent.createObject(control)
            contextMenu.x = mouse.x
            contextMenu.y = mouse.y
            contextMenu.open()

        }
    }

    Component {
        id: contextMenuComponent
        ContextMenu {
            modal: true
            dim: false
            enabled: parent.copyMenuEnabled
            Action {
                //% "Copy"
                text: qsTrId("general-copy")
                icon.source: "qrc:/assets/icon-copy.svg"
                enabled: parent.enabled && parent.copyMenuEnabled
                onTriggered: parent.copyText()
            }
            onOpened: {
                if (parent.enableBackgroundRect)
                    parent.background.visible = true;
                parent.color = parent.selectedTextColor;
            }
            onClosed: {
                parent.color = parent.textColor;
                parent.background.visible = false;
            }
        }
    }

    Component.onCompleted: {
        control.textColor = control.color;
    }
}
