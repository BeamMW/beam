import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Controls.impl 2.4
import QtQuick.Templates 2.4 as T
import "."

T.TextField {
    id: control

    implicitWidth: Math.max(background ? background.implicitWidth : 0,
                            placeholderText ? placeholder.implicitWidth + leftPadding + rightPadding : 0)
                            || contentWidth + leftPadding + rightPadding
    implicitHeight: Math.max(contentHeight + topPadding + bottomPadding,
                             background ? background.implicitHeight : 0,
                             placeholder.implicitHeight + topPadding + bottomPadding)

    font { 
        family: "SF Pro Display"
        styleName: "Regular"
    }

    padding: 6
    leftPadding: 0

    color: control.palette.text
    selectionColor: control.palette.highlight
    selectedTextColor: control.palette.highlightedText
    verticalAlignment: TextInput.AlignVCenter

	selectByMouse: true
	
    PlaceholderText {
        id: placeholder
        x: control.leftPadding
        y: control.topPadding
        width: control.width - (control.leftPadding + control.rightPadding)
        height: control.height - (control.topPadding + control.bottomPadding)

        text: control.placeholderText
        font: control.font
        opacity: 0.5
        color: control.color
        verticalAlignment: control.verticalAlignment
        visible: !control.length && !control.preeditText && (!control.activeFocus || control.horizontalAlignment !== Qt.AlignHCenter)
        elide: Text.ElideRight
    }

    background: Rectangle {
	    id: backgroundRect
        y: control.height - height - control.bottomPadding + 8
        implicitWidth: 120
        height: control.activeFocus || control.hovered ? 2 : 1
        color: Style.white
		opacity: (control.activeFocus || control.hovered)? 0.3 : 0.1
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        hoverEnabled: true

        onClicked: {
            var selectStart = control.selectionStart
            var selectEnd = control.selectionEnd
            var curPos = control.cursorPosition
            contextMenu.x = mouse.x
            contextMenu.y = mouse.y
            contextMenu.open()
            control.cursorPosition = curPos
            control.select(selectStart, selectEnd)
        }
    }

    ContextMenu {
        id: contextMenu
        modal: true
        dim: false
        Action {
            text: qsTr("copy")
            icon.source: "qrc:///assets/icon-copy.svg"
            enabled: control.enabled && (control.selectedText.length > 0) && (control.echoMode === TextInput.Normal)
            onTriggered: {
                control.copy()
            }
        }
        Action {
            text: qsTr("paste")
            icon.source: "qrc:///assets/icon-edit.svg"
            enabled: control.canPaste
            onTriggered: {
                control.paste()
            }
        }
    }
}
