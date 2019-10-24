import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Layouts 1.3
import QtQuick.Window 2.2
import "controls"
import Beam.Wallet 1.0

Window  {
    id: appWindow
    property alias source: rootLoader.source
    flags: Qt.Window | Qt.WindowFullscreenButtonHint

    function cellResize() {
        if(appWindow.visibility != ApplicationWindow.Maximized) {
            var minWidth = Math.min(1024, appWindow.screen.desktopAvailableWidth - 10);
            var minHeight = Math.min(867, appWindow.screen.desktopAvailableHeight - 80);
            appWindow.minimumWidth = minWidth;
            appWindow.minimumHeight = minHeight;
            appWindow.width = minWidth;
            appWindow.height = minHeight;
        }
    }
 
    onVisibilityChanged: cellResize()

    SFFontLoader {}
    
    Popup {
        id: notifications
        closePolicy: Popup.NoAutoClose
        palette.window : Style.background_main
		MessagesViewModel {id: viewModel}

        parent: Overlay.overlay
        width: parent.width
        height: Math.min(100, viewModel.messages.length * 30 + 10)
        clip: true
        visible:  viewModel.messages.length > 0
        background: Item {
            anchors.fill: parent
            Rectangle {
                color: "red"
                opacity: 0.4
                anchors.fill: parent
            }
            ListView {
                id: sampleListView
                anchors.fill: parent
                anchors.topMargin: 5
                anchors.bottomMargin: 5
                //spacing: 4
                clip: true
                model: viewModel.messages

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
                        color: Style.content_main
                        height: 16
                    }
                    CustomToolButton {
                        Layout.alignment: Qt.AlignVCenter
                        Layout.minimumHeight: 20
                        Layout.minimumWidth: 20
                        Layout.rightMargin: 30
                        icon.source: "qrc:/assets/icon-cancel.svg"
                        onClicked: viewModel.deleteMessage(index)
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
        source : "qrc:/start.qml"
        signal activated()
    }

    onActiveChanged: {
        if (this.active) {
            rootLoader.activated();
        }
    }
}
