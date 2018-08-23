import QtQuick 2.3
import QtQuick.Controls 2.4
import QtQuick.Layouts 1.3
import "controls"

Item {
    width: 1024
    height: 768
    property alias source: rootLoader.source

    Popup {
        id: notifications
        closePolicy: Popup.NoAutoClose
        palette.window : Style.marine

        parent: Overlay.overlay
        width: parent.width
        height: Math.min(100, messagesViewModel.messages.length * 30 + 10)
        clip: true
        visible:  messagesViewModel.messages.length > 0
        background: Rectangle {
            color: "red"
            opacity: 0.4
            anchors.fill: parent
            
            ListView {
                id: sampleListView
                anchors.fill: parent
                anchors.topMargin: 5
                anchors.bottomMargin: 5
                //spacing: 4
                clip: true
                model: messagesViewModel.messages

                delegate: RowLayout {
                    width: parent.width
                    height: 30

                    SFText {
                        Layout.fillWidth: true
                        Layout.leftMargin: 30
                        Layout.minimumWidth: 100
                        Layout.alignment: Qt.AlignVCenter
                        text: modelData
                        font.pixelSize: 14
                        color: Style.white
                        height: 16
                    }
                    CustomButton {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.minimumHeight: 20
                        Layout.minimumWidth: 20
                        Layout.rightMargin: 30
                        textOpacity: 0
                        icon.source: "qrc:///assets/icon-cancel.svg"
                        onClicked: messagesViewModel.deleteMessage(index)
                    }
                }
            }
        }
    }

    Loader {
        id: rootLoader
        width: parent.width
        height: parent.height
	    focus: true
        source : "qrc:///start.qml"
    }
}
