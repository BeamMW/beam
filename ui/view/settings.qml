import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import "controls"

Rectangle {

    anchors.fill: parent
    color: "#032e48"

    ConfirmationDialog {
        id: emergencyConfirmation
        text: "Do you really want to reset your wallet?"
        okButtonText: "reset"
        onAccepted: settingsViewModel.emergencyReset()
    }

	SFText {
        font.pixelSize: 36
        color: Style.white
        text: "Settings"
    }

	Rectangle {
        anchors.fill: parent
        anchors.topMargin: 97
        anchors.bottomMargin: 30

        radius: 10
        color: Style.dark_slate_blue

        SFText {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: 30
            anchors.rightMargin: 30

            font.pixelSize: 14
            color: Style.white
            text: "Version: " + settingsViewModel.version
        }

        Column {
            width: 400

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: 30
            anchors.leftMargin: 30

            spacing: 30

    		Column {
                clip: true

                spacing: 10

                SFText {
                    text: "Node IP address and port"
                    color: Style.white
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                SFTextInput {
                    id: nodeAddress
                    width: parent.width
    				focus: true
    				activeFocusOnTab: true
                    font.pixelSize: 12
                    color: Style.white
                    validator: RegExpValidator { regExp: /^(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])(:([0-9]|[1-9][0-9]{1,3}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?$/ }
    				onAccepted: settingsViewModel.applyChanges(nodeAddress.text)
                }

                Rectangle {
                    width: parent.width
                    height: 1

                    color: Style.white
                    opacity: 0.1
                }

                SFText {
                    id: nodeAddressError
                    color: Style.validator_color
                    font.pixelSize: 10
                }
            }  

            PrimaryButton {
                text: "EMERGENCY RESET"
                palette.button : "red"
                palette.buttonText : "white"

                onClicked: emergencyConfirmation.open();
            }        
        }

		Row {
			anchors.horizontalCenter: parent.horizontalCenter
			anchors.bottomMargin: 30
			anchors.bottom: parent.bottom
			spacing: 30

			CustomButton {
				text: "cancel"
				onClicked: {
					nodeAddress.text = settingsViewModel.nodeAddress
				}
			}

			PrimaryButton {		
				text: "apply changes"
				enabled: {nodeAddress.text != settingsViewModel.nodeAddress && nodeAddress.acceptableInput}
				onClicked: settingsViewModel.applyChanges(nodeAddress.text)
			}
		}
	}

	Component.onCompleted: {
        nodeAddress.text = settingsViewModel.nodeAddress
    }
}
