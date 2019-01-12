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
        color: Style.dark_slate_blue
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
				text: qsTr("Comment")
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

        Column {
    		width: parent.width

			SFText {
				text: qsTr("Expires")
				color: Style.white
				font.pixelSize: 12
				font.styleName: "Bold"; font.weight: Font.Bold
			}

			CustomComboBox {
                id: expires
                width: 140
                height: 20
                currentIndex: rootControl.addressItem && rootControl.addressItem.neverExpired ? 1 : 0

                model: rootControl.isExpiredAddress ? ["in 24 hours (since now)", "never"] : ["in 24 hours (since now)", "never", "now"]
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
                enabled: {
                    if (rootControl.isExpiredAddress)
                        return true
                    else if (rootControl.addressItem) {
                        if (rootControl.addressItem.neverExpired && expires.currentIndex != 1)
                            return true
                        else if (!rootControl.addressItem.neverExpired && expires.currentIndex != 0)
                            return true;
                    }
                    return false;
                }
                onClicked: {
                    parentModel.saveChanges(addressID.text, addressName.text, expires.currentIndex == 1, rootControl.isExpiredAddress, expires.currentIndex == 2);
                    rootControl.accepted();
                    rootControl.close();
                }
			}
		}
    }
}