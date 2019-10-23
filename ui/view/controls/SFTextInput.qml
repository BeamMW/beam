import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Controls.impl 2.4
import QtQuick.Templates 2.4 as T
import "."

T.TextField {
    id: control
    property var onPaste: function() {}

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

    property alias backgroundColor : backgroundRect.color
    property alias underlineVisible : backgroundRect.visible
    backgroundColor: Style.content_main

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
        horizontalAlignment: control.horizontalAlignment
        visible:  !control.activeFocus && !control.length && !control.preeditText
        elide: Text.ElideRight
        wrapMode: control.wrapMode
    }

    background: Rectangle {
	    id: backgroundRect
        y: control.height - height - control.bottomPadding + 4
        width: control.width - (control.leftPadding + control.rightPadding)
        height: control.activeFocus || control.hovered ? 1 : 1
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
            //% "Copy"
            text: qsTrId("general-copy")
            icon.source: "qrc:/assets/icon-copy.svg"
            enabled: control.enabled && (control.echoMode === TextInput.Normal)
            onTriggered: {
                if (control.selectedText.length > 0) {
                    control.copy();
                }
                else {
                    control.selectAll();
                    control.copy();
                    control.deselect();
                }
            }
        }
        Action {
            //% "Paste"
            text: qsTrId("general-paste")
            icon.source: "qrc:/assets/icon-edit.svg"
            enabled: control.canPaste
            onTriggered: {
                control.onPaste();
                control.paste();
            }
        }
    }

    Keys.onPressed: function(keyEvent) {
        if (keyEvent.matches(StandardKey.Paste)) {
            control.onPaste();
        }
    }
}
