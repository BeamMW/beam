import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Controls.impl 2.4
import QtQuick.Templates 2.4 as T
import Beam.Wallet 1.0
import "."

T.TextField {
    id: control

    font.family:           "SF Pro Display"
    font.styleName:        "Regular"
    padding:               6
    leftPadding:           0
    font.italic:           text.length && !acceptableInput
    color:                 placeholder.visible ? "transparent" : (control.isValid ? Style.content_main : Style.validator_error)
    selectionColor:        control.palette.highlight
    selectedTextColor:     control.palette.highlightedText
    verticalAlignment:     TextInput.AlignVCenter
    horizontalAlignment:   control.activeFocus || contextMenu.visible ? Text.AlignLeft : Text.AlignHCenter
	selectByMouse:         true
	validator:             ELSeedValidator {}
	wrapMode:              TextInput.Wrap

	property bool isValid: text.length ? acceptableInput : true
    signal newSeed

    background: Rectangle {
	    id:      backgroundRect
        y:       control.height - height - control.bottomPadding + 4
        width:   control.width - (control.leftPadding + control.rightPadding)
        height:  control.activeFocus || control.hovered ? 1 : 1
		opacity: (control.activeFocus || control.hovered)? 0.3 : 0.1
		color:   control.isValid ?  Style.content_secondary : Style.validator_error
    }

    MouseArea {
        anchors.fill:    parent
        acceptedButtons: Qt.RightButton
        hoverEnabled:    true

        onClicked: {
            if (!control.activeFocus && control.text.length) return
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

    MouseArea {
        anchors.fill: parent;
        acceptedButtons: Qt.LeftButton
        onDoubleClicked: if (control.text.length == 0) newSeed()
        onClicked: parent.forceActiveFocus()
    }

    ContextMenu {
        id:    contextMenu
        modal: true
        dim:   false

        onClosed: {
            if(control.text.length) control.forceActiveFocus()
        }

        Action {
            //% "Copy"
            text:        qsTrId("general-copy")
            icon.source: "qrc:/assets/icon-copy.svg"
            enabled:     control.enabled && (control.echoMode === TextInput.Normal)
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
            text:        qsTrId("general-paste")
            icon.source: "qrc:/assets/icon-edit.svg"
            enabled:     control.canPaste
            onTriggered: {
                control.paste()
            }
        }
    }

    PlaceholderText {
        id: placeholder
        x: control.leftPadding
        y: control.topPadding

        width:    control.width - (control.leftPadding + control.rightPadding)
        height:   control.height - (control.topPadding + control.bottomPadding)
        text:     control.placeholderText
        font:     control.font
        opacity:  control.isValid ? 1 : 0.5
        color:    control.isValid ? Style.content_main : Style.validator_error
        visible:  (text.length > 0 && !control.activeFocus && !contextMenu.visible) || (!control.activeFocus && !contextMenu.visible && !control.length && !control.preeditText)
        elide:    Text.ElideRight
        wrapMode: control.wrapMode

        verticalAlignment:   control.verticalAlignment
        horizontalAlignment: control.horizontalAlignment
    }
}
