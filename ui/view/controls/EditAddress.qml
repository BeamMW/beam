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
        color: Style.background_second
        anchors.fill: parent
    }

    contentItem: Column {
        anchors.fill: parent
    	anchors.margins: 30

    	spacing: 30

        SFText {
			anchors.horizontalCenter: parent.horizontalCenter
			//% "Edit address"
			text: qsTrId("edit-addr-title")
			color: Style.content_main
			font.pixelSize: 18
			font.styleName: "Bold"; font.weight: Font.Bold
		}

        Column {
    		width: parent.width

			SFText {
				//% "Address ID"
				text: qsTrId("edit-addr-addr-id")
				color: Style.content_main
				font.pixelSize: 12
				font.styleName: "Bold"; font.weight: Font.Bold
			}

			SFTextInput {
				id: addressID

				width: parent.width
                enabled: false
				font.pixelSize: 12
				color: Style.content_main
                text: rootControl.addressItem ? rootControl.addressItem.address : ""
			}
    	}

        Column {
    		width: parent.width

			SFText {
				//% "Comment"
				text: qsTrId("edit-addr-comment")
				color: Style.content_main
				font.pixelSize: 12
				font.styleName: "Bold"; font.weight: Font.Bold
			}

			SFTextInput {
				id: addressName

				width: parent.width
				font.pixelSize: 12
				color: Style.content_main
                enabled: false
                text: rootControl.addressItem ? rootControl.addressItem.name : ""
			}
    	}

        Column {
    		width: parent.width

			SFText {
				//% "Expires"
				text: qsTrId("edit-addr-expires")
				color: Style.content_main
				font.pixelSize: 12
				font.styleName: "Bold"; font.weight: Font.Bold
			}

			CustomComboBox {
                id: expires
                width: 140
                height: 20
                currentIndex: rootControl.addressItem && rootControl.addressItem.neverExpired ? 1 : 0

                model: rootControl.isExpiredAddress ? ["in 24 hours (from now)", "never"] : ["in 24 hours (from now)", "never", "now"]
            }
    	}

        Row {
			anchors.horizontalCenter: parent.horizontalCenter
			spacing: 30

			CustomButton {
				//% "cancel"
				text: qsTrId("edit-addr-cancel-button")
                icon.source: "qrc:/assets/icon-cancel.svg"
                icon.color: Style.content_main
				onClicked: {
                    rootControl.close();
                }
			}

			PrimaryButton {
				text: rootControl.isExpiredAddress ?
						//% "make active"
					  	qsTrId("edit-addr-make-active-button") :
						//% "save"
						qsTrId("edit-addr-save-button")
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
                    parentModel.saveChanges(addressID.text, addressName.text, expires.currentIndex == 1, expires.currentIndex == 0, expires.currentIndex == 2);
                    rootControl.accepted();
                    rootControl.close();
                }
			}
		}
    }
}