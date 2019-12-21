import QtQuick 2.11
import QtQuick.Controls 1.4
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import QtQuick.Window 2.2
import "controls"
import Beam.Wallet 1.0
import "utils.js" as Utils

Rectangle {
    id: main

    anchors.fill: parent

	MainViewModel {id: viewModel}

    ConfirmationDialog {
        id:                     closeDialog
        //% "Beam wallet close"
        title:                  qsTrId("app-close-title")
        //% "There are %1 active transactions that might fail if the wallet will go offline. Are you sure to close the wallet now?"
        text:                   qsTrId("app-close-text").arg(viewModel.unsafeTxCount)
        //% "yes"
        okButtonText:           qsTrId("atomic-swap-tx-yes-button")
        okButtonIconSource:     "qrc:/assets/icon-done.svg"
        okButtonColor:          Style.swapCurrencyStateIndicator
        //% "no"
        cancelButtonText:       qsTrId("atomic-swap-no-button")
        cancelButtonIconSource: "qrc:/assets/icon-cancel-16.svg"
        
        onOpened: {
            closeDialog.visible = Qt.binding(function(){return viewModel.unsafeTxCount > 0;});
        }

        onClosed: {
            closeDialog.visible = false;
        }

        onAccepted: {
            Qt.quit();
        }
        modal: true
    }

    OpenExternalLinkConfirmation {
        id: externalLinkConfirmation
    }

    SettingsViewModel {
        id: settingsViewModel
    }

    function onClosing (close) {
        if (viewModel.unsafeTxCount > 0) {
            close.accepted = false;
            closeDialog.open();
        }
    }

    property color topColor: Style.background_main_top
    property color topGradientColor: Qt.rgba(Style.background_main_top.r, Style.background_main_top.g, Style.background_main_top.b, 0)

    StatusbarViewModel {
        id: statusbarModel
    }

    property alias backgroundRect: mainBackground
    Rectangle {
        id: mainBackground
        anchors.fill: parent
        color: Style.background_main

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            height: 230
            gradient: Gradient {
                GradientStop { position: 0.0; color: main.topColor }
                GradientStop { position: 1.0; color: main.topGradientColor }
            }
        }
    }

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
        "atomic_swap",
		"addresses", 
		"utxo",
		//"notification", 
		//"info",
		"settings"]
    property int selectedItem

    Item {
        id: sidebar
        width: 70
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.top: parent.top

        Rectangle {
            anchors.fill: parent
            color: Style.navigation_background
            opacity: 0.1
            border.width: 0
        }

        Column {
            width: 0
            height: 0
            anchors.right: parent.right
            anchors.rightMargin: 0
            anchors.left: parent.left
            anchors.leftMargin: 0
            anchors.top: parent.top
            anchors.topMargin: 130

            Repeater{
                id: controls
                model: contentItems

                Item {
                    id: control
                    width: parent.width
                    height: 66
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
                            color: selectedItem == index ? Style.active : Style.passive
                        }

                        DropShadow {
                            anchors.fill: indicator
                            radius: 5
                            samples: 9
                            color: Style.active
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

        SvgImage {
            id: image
            y:  50
            anchors.horizontalCenter: parent.horizontalCenter
            source: "qrc:/assets/logo.svg"
            smooth: true
        }

        Item {
            property bool clicked: false
            id: whereToBuyControl
            width: parent.width
            anchors.bottom: parent.bottom
            height: 66
            activeFocusOnTab: true

            function clickHandler() {
                whereToBuyControl.clicked = true;
            }

            onClickedChanged: {
                if (clicked) {
                    Utils.openExternal(
                        "https://www.beam.mw/#exchanges",
                        settingsViewModel,
                        externalLinkConfirmation,
                        function () {
                            console.log("onFinish");
                            whereToBuyControl.clicked = false;
                        });
                }
            }

            SvgImage {
                x: 21
                y: 16
                width: 28
                height: 28
                source: whereToBuyControl.clicked
                    ? "qrc:/assets/icon-where-to-buy-beam-green.svg"
                    : "qrc:/assets/icon-where-to-buy-beam-gray.svg"
            }
            Item {
                Rectangle {
                    id: indicator
                    y: 6
                    width: 4
                    height: 48
                    color: whereToBuyControl.clicked ? Style.active : Style.passive
                }

                DropShadow {
                    anchors.fill: indicator
                    radius: 5
                    samples: 9
                    color: Style.active
                    source: indicator
                }

                visible: whereToBuyControl.activeFocus
            }
            Keys.onPressed: {
                if ((event.key == Qt.Key_Return || event.key == Qt.Key_Enter || event.key == Qt.Key_Space) && whereToBuyControl.activeFocus)
                    whereToBuyControl.clickHandler();
            }

            MouseArea {
                id: mouseArea
                anchors.fill: parent
                onClicked: {
                    whereToBuyControl.clickHandler();
                }
                hoverEnabled: true
            }
        }

    }

    Loader {
        id: content
        anchors.topMargin: 50
        anchors.bottomMargin: 0
        anchors.rightMargin: 20
        anchors.leftMargin: 90
        anchors.fill: parent
        focus: true
    }

    function updateItem(indexOrID, props)
    {
        var update = function(index) {
            selectedItem = index
            controls.itemAt(index).focus = true;
            content.setSource("qrc:/" + contentItems[index] + ".qml", Object.assign({"toSend": false}, props))
            viewModel.update(index)
        }

        if (typeof(indexOrID) == "string") {
            for (var index = 0; index < contentItems.length; index++) {
                if (contentItems[index] == indexOrID) {
                    return update(index);
                }
            }
        }

        // plain index passed
        update(indexOrID)
    }

	function openSendDialog() {
		updateItem("wallet", {"toSend": true})
	}

	function openSwapSettings() {
        updateItem("settings", {swapMode:true})
    }

    function openSwapActiveTransactionsList() {
        updateItem("atomic_swap", {"shouldShowActiveTransactions": true})
    }

    function resetLockTimer() {
        viewModel.resetLockTimer();
    }

    property var trezor_popup

    Connections {
        target: viewModel
        onGotoStartScreen: { 
            main.parent.setSource("qrc:/start.qml", {"isLockedMode": true});
        }

        onShowTrezorMessage:{
            trezor_popup = Qt.createComponent("popup_message.qml").createObject(main)

            //% "Please, look at your Trezor device to complete actions..."
            trezor_popup.message = qsTrId("trezor-message")
            trezor_popup.open()
        }

        onHideTrezorMessage:{
            trezor_popup.close()
        }

        onShowTrezorError: function(error) {
            console.log(error)
            trezor_popup = Qt.createComponent("popup_message.qml").createObject(main)
            trezor_popup.message = error
            trezor_popup.open()

        }
    }

    Component.onCompleted: {
        updateItem("wallet")
        main.Window.window.closing.connect(onClosing)
    }

    Component.onDestruction: {
        main.Window.window.closing.disconnect(onClosing)
    }

}
