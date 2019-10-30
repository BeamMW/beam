import QtQuick 2.11
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.0
import "."
import Beam.Wallet 1.0

Dialog {
	id: control

	SettingsViewModel {id: viewModel}
	property string dialogTitle: "title"
	property string dialogMessage: "message"
	property bool showError: false
	property var onDialogAccepted: function() {
		console.log("Accepted");
	}
	property var onDialogRejected: function() {
		console.log("Rejected");
	}
	property var onPwdEntered: function(password) {
		if (viewModel.checkWalletPassword(password)) {
			accept();
		} else {
			showError = true;
		}
	}

	modal: true

	width: 460
	height: 243
	x: (parent.width - width) / 2
	y: (parent.height - height) / 2
	visible: false

	background: Rectangle {
		radius: 10
        color: Style.background_popup
        anchors.fill: parent            
    }

    contentItem: ColumnLayout {
    	anchors.fill: parent
    	anchors.margins: 30

    	spacing: 30

		SFText {
			Layout.alignment: Qt.AlignHCenter
			Layout.fillWidth: true
			text: dialogTitle
			color: Style.content_main
			horizontalAlignment: Text.AlignHCenter
			font.pixelSize: 18
			font.styleName: "Bold"; font.weight: Font.Bold
		}

		ColumnLayout {
			Layout.topMargin: 0
			Layout.preferredWidth: parent.width - 60
			SFText {
				Layout.alignment: Qt.AlignHCenter
				text: dialogMessage
				color: Style.content_main
				font.pixelSize: 14
			}

			SFTextInput {
				id: pwd
				Layout.alignment: Qt.AlignHCenter
				Layout.fillWidth: true
				width: parent.width
				font.pixelSize: 14
				color: showError ? Style.validator_error : Style.content_main
				backgroundColor: showError ? Style.validator_error : Style.content_main
				echoMode: TextInput.Password
				onTextEdited: {
					showError = false;
				}
				Keys.onEnterPressed: {
					onPwdEntered(text);
				}
				Keys.onReturnPressed: {
					onPwdEntered(text);
				}
			}  		

			Item {
				Layout.preferredHeight: 16
				Layout.topMargin: -5
				SFText {
					Layout.fillWidth: true
					Layout.alignment: Qt.AlignHCenter
					color: Style.validator_error
					font.pixelSize: 12
					//% "Please, enter password"
					text: qsTrId("general-pwd-empty-error")
					visible: showError && !pwd.text.length
				}
				SFText {
					Layout.fillWidth: true
					Layout.alignment: Qt.AlignHCenter
					color: Style.validator_error
					font.pixelSize: 12
					//% "Invalid password provided"
					text: qsTrId("general-pwd-invalid")
					visible: showError && pwd.text.length
				}
			}
		}			 	

		RowLayout {
			spacing: 30
			Layout.topMargin: -10
			Layout.alignment: Qt.AlignHCenter

			CustomButton {
				Layout.preferredHeight: 38
				//% "Cancel"
				text: qsTrId("general-cancel")
				icon.source: "qrc:/assets/icon-cancel.svg"
				onClicked: reject()
			}

			PrimaryButton {
				Layout.preferredHeight: 38
				//: confirm password dialog, ok button
				//% "Proceed"
				text: qsTrId("general-proceed")
				enabled: !showError
				icon.source: "qrc:/assets/icon-done.svg"
				onClicked: {
					onPwdEntered(pwd.text);
				}
			}
		}
    }

	onOpened: {
		pwd.text = "";
		showError = false;
		pwd.forceActiveFocus(Qt.TabFocusReason);
	}

	onAccepted: {
		if (typeof onDialogRejected == "function") {
			onDialogAccepted();
		}
	}
	onRejected: {
		if (typeof onDialogRejected == "function") {
			onDialogRejected();
		}
	}	
}
