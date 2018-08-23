import QtQuick 2.3
import QtQuick.Controls 2.4
import QtQuick.Layouts 1.3

/*Item {
    width: 1024
    height: 768*/

    /*Popup {
        id: notifications
        closePolicy: Popup.NoAutoClose

        parent: Overlay.overlay
        width: parent.width
        height: 100
        clip: true
        visible:  messagesViewModel.messages.length > 0
        ListView {
            id: sampleListView
            anchors.fill: parent
            model: messagesViewModel.messages
            delegate: RowLayout {
                width: parent.width
                height: 30

                Text {
                    width: 100
                    text: modelData
                    height: 16
                }
                Button {
                    text: "delete"
                    height: 30
                    width: 60
                    onClicked: messagesViewModel.deleteMessage(index)
                }
            }
            spacing: 4
        }
    }*/

    Loader {

        width: 1024
        height: 768
	    focus: true
        source : "qrc:///start.qml"
    }
//}
