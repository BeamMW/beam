import QtQuick 2.11
import QtQuick.Controls 2.3
import "."

Dialog {
	id: rootControl

    property var parentModel 
    property bool isExpiredAddress: false
    property var addressItem: null

    modal: true
    visible: false

    width: 460
    height: 350

    x: (parent.width - width) / 2
	y: (parent.height - height) / 2

    background: Rectangle {
		radius: 10
        color: Style.combobox_color
        anchors.fill: parent            
    }

    contentItem: Column {
        anchors.fill: parent
    	anchors.margins: 30

    	spacing: 30

        SFText {
			anchors.horizontalCenter: parent.horizontalCenter
			text: qsTr("Edit address")
			color: Style.white
			font.pixelSize: 18
			font.styleName: "Bold"; font.weight: Font.Bold
		}

        Column {
    		width: parent.width

			SFText {
				text: qsTr("Address ID")
				color: Style.white
				font.pixelSize: 12
				font.styleName: "Bold"; font.weight: Font.Bold
			}

			SFTextInput {
				id: addressID

				width: parent.width
                enabled: false
				font.pixelSize: 12
				color: Style.white
                text: rootControl.addressItem ? rootControl.addressItem.address : ""
			}
    	}

        Column {
    		width: parent.width

			SFText {
				text: qsTr("Name")
				color: Style.white
				font.pixelSize: 12
				font.styleName: "Bold"; font.weight: Font.Bold
			}

			SFTextInput {
				id: addressName

				width: parent.width
				font.pixelSize: 12
				color: Style.white
                enabled: false
                text: rootControl.addressItem ? rootControl.addressItem.name : ""
			}
    	}

        Row {
            width: parent.width

            spacing: 60
            
            ButtonGroup {
                id: expirationGroup
            }

            CustomRadioButton {
                id: exp1
                text: rootControl.addressItem && (rootControl.addressItem.neverExpired || rootControl.isExpiredAddress) ? qsTr("24h since now") : qsTr("24h")
                ButtonGroup.group: expirationGroup
                font.pixelSize: 12
                checked:  rootControl.addressItem ? !rootControl.addressItem.neverExpired : false
                outerWidth: 10
                innerWidth: 6
            }

            CustomRadioButton {
                id: exp2
                text: qsTr("Never")
                ButtonGroup.group: expirationGroup
                font.pixelSize: 12
                checked:  rootControl.addressItem ? rootControl.addressItem.neverExpired : false
                outerWidth: 10
                innerWidth: 6
            }
        }

        Row {
			anchors.horizontalCenter: parent.horizontalCenter
			spacing: 30

			CustomButton {
				text: qsTr("cancel")
                icon.source: "qrc:/assets/icon-cancel.svg"
                icon.color: Style.white
				onClicked: {
                    rootControl.close();
                }
			}

			PrimaryButton {
				text: rootControl.isExpiredAddress ? qsTr("make active") : qsTr("save")
                icon.source: "qrc:/assets/icon-done.svg"
                enabled: rootControl.isExpiredAddress || (rootControl.addressItem ? rootControl.addressItem.neverExpired != exp2.checked : false)
                onClicked: {
                    parentModel.saveChanges(addressID.text ,addressName.text, exp2.checked, rootControl.isExpiredAddress);
                    rootControl.accepted();
                    rootControl.close();
                }
			}
		}
    }
}