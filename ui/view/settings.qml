import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import "controls"
import Beam.Wallet 1.0

Rectangle {

    anchors.fill: parent
    color: "#032e48"

	SettingsViewModel {id: viewModel}

    ConfirmationDialog {
        id: emergencyConfirmation
        text: "Do you really want to reset your wallet?"
        okButtonText: "reset"
        onAccepted: viewModel.emergencyReset()
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
            text: "Version: " + viewModel.version
        }

        Column {
            width: 400

            anchors.top: parent.top
            anchors.left: parent.left
            anchors.topMargin: 30
            anchors.leftMargin: 30

            spacing: 30

    		Column {
                width: parent.width
                spacing: 10

                SFText {
                    text: qsTr("Connect to node (ip:port)")
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
                    color: readOnly ? Style.disable_text_color : Style.white
                    readOnly: localNodeRun.checked
                    validator: RegExpValidator { regExp: /^(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])(:([0-9]|[1-9][0-9]{1,3}|[1-5][0-9]{4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]))?$/ }
    				text: viewModel.nodeAddress
                    Binding {
                        target: viewModel
                        property: "nodeAddress"
                        value: nodeAddress.text
                    }
                }

                SFText {
                    id: nodeAddressError
                    color: Style.validator_color
                    font.pixelSize: 10
                    visible: !nodeAddress.acceptableInput
                    text: "Invalid address"
                }

                CustomSwitch {
                    id: localNodeRun
                    text: qsTr("Run local node")
                    font.pixelSize: 12
                    width: parent.width
                    checked: viewModel.localNodeRun
                    Binding {
                        target: viewModel
                        property: "localNodeRun"
                        value: localNodeRun.checked
                    }
                }

                SFText {
                    text: qsTr("Local node port")
                    color: Style.white
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                SFTextInput {
                    id: localNodePort
                    width: parent.width
    			    activeFocusOnTab: true
                    font.pixelSize: 12
                    color: readOnly ? Style.disable_text_color : Style.white
                    readOnly: !localNodeRun.checked
                    text: viewModel.localNodePort
                    validator: IntValidator {
                        bottom: 0
                        top: 65535
                    }
                    Binding {
                        target: viewModel
                        property: "localNodePort"
                        value: localNodePort.text
                    }
                }

                SFText {
                    text: qsTr("Mining threads")
                    color: Style.white
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                SFTextInput {
                    id: localNodeMiningThreads
                    width: parent.width
    			    activeFocusOnTab: true
                    font.pixelSize: 12
                    color: readOnly ? Style.disable_text_color : Style.white
                    readOnly: !localNodeRun.checked
                    text: viewModel.localNodeMiningThreads
                    validator: IntValidator {
                        bottom: 0
                        top: viewModel.coreAmount()
                    }
                    Binding {
                        target: viewModel
                        property: "localNodeMiningThreads"
                        value: localNodeMiningThreads.text
                    }
                }

                SFText {
                    text: qsTr("Verification threads")
                    color: Style.white
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }

                SFTextInput {
                    id: localNodeVerificationThreads
                    width: parent.width
    			    activeFocusOnTab: true
                    font.pixelSize: 12
                    color: readOnly ? Style.disable_text_color : Style.white
                    readOnly: !localNodeRun.checked
                    text: viewModel.localNodeVerificationThreads
                    validator: IntValidator {
                        bottom: 0
                        top: viewModel.coreAmount()
                    }
                    Binding {
                        target: viewModel
                        property: "localNodeVerificationThreads"
                        value: localNodeVerificationThreads.text
                    }
                }
            }  

            PrimaryButton {
                text: "emergency reset"
                //palette.button : Style.dark_slate_blue
                width: 198
                palette.button: "red"
                palette.buttonText : "white"
                icon.source: "qrc:///assets/icon-reset.svg"
                icon.width: 16
                icon.height: 16
                onClicked: emergencyConfirmation.open();
            }
			
			PrimaryButton {
                text: "report problem"
                palette.buttonText : "white"
                //palette.button : Style.dark_slate_blue
                onClicked: viewModel.reportProblem()
            } 
        }

		Row {
			anchors.horizontalCenter: parent.horizontalCenter
			anchors.bottomMargin: 30
			anchors.bottom: parent.bottom
			spacing: 30

			CustomButton {
				text: qsTr("undo changes")
				onClicked: viewModel.undoChanges()
			}

			PrimaryButton {		
				text: qsTr("apply changes")
				enabled: {
                    viewModel.isChanged 
                    && nodeAddress.acceptableInput
                    && localNodePort.acceptableInput
                    && localNodeMiningThreads.acceptableInput
                    && localNodeVerificationThreads.acceptableInput}
				onClicked: viewModel.applyChanges()
			}
		}
	}
}
