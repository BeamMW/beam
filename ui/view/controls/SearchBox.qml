import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Layouts 1.4
import Beam.Wallet 1.0
import "."

FocusScope {
    id: control

    property alias placeholderText:     input.placeholderText
    property alias text:                input.text
    implicitHeight: 32
    implicitWidth:  400

    Rectangle {
        id: rect
        anchors.fill:     parent
        opacity:          0.1
        border.color:     Style.content_main
        border.width:     1
        color:            "transparent"
        radius:           16
        visible:          input.visible
    }

    RowLayout {
        anchors.fill:   parent
        spacing:        0
        SFTextInput {
            id:                      input
            Layout.fillWidth:        true
            Layout.preferredHeight:  32
            font.pixelSize:          14
            leftPadding:             15
            rightPadding:            15
            color:                   Style.content_main
            visible:                 false
            inputMethodHints:        Qt.ImhNoPredictiveText
            focusablePlaceholder:    true
            background: Item {}
            onFocusChanged: {
                if (!focus && text.length == 0 && !searcIconBtn.focus) {
                    visible = false;
                }
            }
            onTextChanged: {
                if (!focus && text.length == 0) {
                    visible = false;
                }
            }
        }

        CustomToolButton {
            Layout.alignment: Qt.AlignTop 
            icon.source: "qrc:/assets/icon-cancel-16.svg"
            visible:     input.text.length > 0
            //% "Clear search"
            ToolTip.text: qsTrId("wallet-clear-search")
            onClicked: {
                input.text = "";
            }
        }

        CustomToolButton {
            id: searcIconBtn
            Layout.alignment: Qt.AlignTop | Qt.AlignRight
            icon.source: "qrc:/assets/icon-search.svg"
            visible:     input.text.length == 0
            //% "Search"
            ToolTip.text: qsTrId("wallet-search")
            onClicked: {
                if (input.text.length == 0) {
                    input.visible = !input.visible
                    if(input.visible) input.forceActiveFocus()
                }
            }
        }
    }
}



