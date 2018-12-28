import QtQuick 2.11
import QtQuick.Controls 1.4
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import "controls"
import Beam.Wallet 1.0

Rectangle {
    id: main

    anchors.fill: parent

	MainViewModel {id: viewModel}

    StatusbarViewModel {
        id: statusbarModel
    }

    color: Style.marine

    MouseArea {
        id: mainMouseArea
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        propagateComposedEvents: true
        onMouseXChanged: resetLockTimer()
        onPressedChanged: resetLockTimer()
    }

    Keys.onReleased: {
        resetLockTimer()
    }

    property var contentItems : [
		//"dashboard",
		"wallet", 
		"addresses", 
		"utxo",
		//"notification", 
		//"info",
		"settings"]
    property int selectedItem

    Rectangle {
        id: sidebar
        width: 70
        height: 0
        color: Style.navy
        border.width: 0
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.top: parent.top


        Column {
            width: 0
            height: 0
            anchors.right: parent.right
            anchors.rightMargin: 0
            anchors.left: parent.left
            anchors.leftMargin: 0
            anchors.top: parent.top
            anchors.topMargin: 125

            Repeater{
                model: contentItems

                Item {
                    id: control
                    width: parent.width
                    height: parent.width
                    activeFocusOnTab: true
                    
                    SvgImage {
						id: icon
                        x: 21
                        y: 16
                        width: 28
                        height: 28
                        source: "qrc:/assets/icon-" + modelData + (selectedItem == index ? "-active" : "") + ".svg"
					}
                    Item {
                        Rectangle {
                            id: indicator
                            y: 6
                            width: 4
                            height: 48
                            color: selectedItem == index ? Style.bright_teal : Style.silver
                        }

                        DropShadow {
                            anchors.fill: indicator
                            radius: 5
                            samples: 9
                            color: Style.bright_teal
                            source: indicator
                        }

    					visible: control.activeFocus
                    }
                    Keys.onPressed: {
                        if ((event.key == Qt.Key_Return || event.key == Qt.Key_Enter || event.key == Qt.Key_Space) && selectedItem != index) 
                            updateItem(index);
                    }

                    MouseArea {
                        id: mouseArea
                        anchors.fill: parent
                        onClicked: {
                            control.focus = true
                            if (selectedItem != index)
                                updateItem(index)
                        }
						hoverEnabled: true
                    }
                }
            }
        }

        Image {
            id: image
            y: 50
            anchors.horizontalCenter: parent.horizontalCenter
            width: 40
            height: 28
            source: "qrc:/assets/logo.svg"
        }

    }

    Loader {
        id: content
        anchors.topMargin: 50
        anchors.bottomMargin: 0
        anchors.rightMargin: 30
        anchors.leftMargin: 100
        anchors.fill: parent
        focus: true
    }

    function updateItem(index)
    {
        selectedItem = index
        content.setSource("qrc:/" + contentItems[index] + ".qml", {"toSend": false})
        viewModel.update(index)
    }

	function openSendDialog() {
		selectedItem = 0
		content.setSource("qrc:/wallet.qml", {"toSend": true})
        
		viewModel.update(selectedItem)
	}

    function resetLockTimer() {
        viewModel.resetLockTimer();
    }

    Connections {
        target: viewModel
        onGotoStartScreen: { 
            main.parent.source = "qrc:/start.qml"
        }
    }

    Component.onCompleted:{
        updateItem(0) // load wallet view by default
    }
}
