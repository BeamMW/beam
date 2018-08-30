import QtQuick 2.3
import QtQuick.Controls 2.3
import "."
import Beam.Wallet 1.0

Dialog {
	id: control

	SettingsViewModel {id: viewModel}

	modal: true

	width: 520
	height: 420
	x: (parent.width - width) / 2
	y: (parent.height - height) / 2
	visible: false

	background: Rectangle {
		radius: 10
        color: Style.dark_slate_blue
        anchors.fill: parent            
    }

    contentItem: Column {
    	anchors.fill: parent
    	anchors.margins: 30

    	spacing: 30

		SFText {
			anchors.horizontalCenter: parent.horizontalCenter
			text: qsTr("Change wallet password")
			color: Style.white
			font.pixelSize: 24
			font.weight: Font.Bold
		}

    	Column
    	{
    		width: parent.width

			SFText {
				text: qsTr("Enter old password")
				color: Style.white
				font.pixelSize: 12
				font.weight: Font.Bold
			}

			SFTextInput {
				id: oldPass

				width: parent.width

				font.pixelSize: 12
				color: Style.white
				echoMode: TextInput.Password
			}    		
    	}

    	Column
    	{
    		width: parent.width

			SFText {
				text: qsTr("Enter new password")
				color: Style.white
				font.pixelSize: 12
				font.weight: Font.Bold
			}

			SFTextInput {
				id: newPass

				width: parent.width

				font.pixelSize: 12
				color: Style.white
				echoMode: TextInput.Password
			}    		
    	}

    	Column
    	{
    		width: parent.width

			SFText {
				text: qsTr("Confirm new password")
				color: Style.white
				font.pixelSize: 12
				font.weight: Font.Bold
			}

			SFTextInput {
				id: confirmPass

				width: parent.width

				font.pixelSize: 12
				color: Style.white
				echoMode: TextInput.Password
			}

    	}

		Column  {
			width: parent.width
			height: error.height

			SFText {
				id: error
				color: Style.validator_color
				font.pixelSize: 10
			}			
		}    	

		Row {
			anchors.horizontalCenter: parent.horizontalCenter
			spacing: 30

			CustomButton {
				text: qsTr("cancel")
				onClicked: control.close()
			}

			PrimaryButton {
				text: qsTr("change password")
				onClicked: {
					if(oldPass.text.length == 0)
					{
						error.text = qsTr("Please, enter old password");
					}
					else if(newPass.text.length == 0)
					{
						error.text = qsTr("Please, enter new password");
					}
					else if(confirmPass.text.length == 0)
					{
						error.text = qsTr("Please, confirm new password");
					}
					else if(newPass.text == oldPass.text)
					{
						error.text = qsTr("New password cannot be the same as old");
					}
					else if(newPass.text != confirmPass.text)
					{
						error.text = qsTr("New password doesn't match the confirm password");
					}
					else if(!viewModel.checkWalletPassword(oldPass.text))
					{
						error.text = qsTr("The old password you have entered is incorrect");
					}
					else
					{
						viewModel.changeWalletPassword(newPass.text)
						control.close()
					}
				}
			}
		}
    }

	onOpened: {
		oldPass.forceActiveFocus(Qt.TabFocusReason);
		oldPass.text = newPass.text = confirmPass.text = error.text = ""
	}		
}