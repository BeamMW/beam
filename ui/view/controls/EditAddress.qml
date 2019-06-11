import QtQuick 2.11
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.11
import "."
import "../utils.js" as Utils

Dialog {
	id: rootControl

    property var parentModel
	property var expirationOptions: [
		//: Edit address dialog, expiration option, in 24 hours from now
		//% "in 24 hours from now"
		qsTrId("edit-addr-24-option"),
		//: Edit address dialog, expiration option, never
		//% "never"
		qsTrId("edit-addr-never-option")
	]
    property bool isExpiredAddress: false
	property bool isAddressWithCommentExist: false
    property var addressItem: null
	property var isNeverExpired: function() {
		return addressItem ? addressItem.neverExpired : false;
	}
	property var reset: function() {
		activate.checked = false;
		disactivate.checked = false;
		if (isExpiredAddress) {
			expirationOptionsForUnactive.model = expirationOptions;
			expirationOptionsForUnactive.currentIndex = 0;
		} else {
			if (isNeverExpired()) {
				expirationOptionsForActive.model = expirationOptions;
				expirationOptionsForActive.currentIndex = 1;
			} else {
				//: Edit address dialog, expiration option, do not change
				//% "within 24 hours"
				expirationOptionsForActive.model = [qsTrId("edit-addr-as-is-option"),].concat(expirationOptions);
			}
		}
	}

	property var getExpirationTimeLabel: function() {
		var localeName = parentModel.getLocaleName();
		if (isExpiredAddress) {
			return addressItem
				? Utils.formatDateTime(addressItem.expirationDate, localeName)
				: "";
		}
		if (disactivate.checked) {
			var datetime = new Date();
			return Utils.formatDateTime(datetime, localeName);
		}
		var index = expirationOptionsForActive.currentIndex;
		if (isNeverExpired()) {
			index++;
		}

		if (index == 0) {
			return addressItem
				? Utils.formatDateTime(addressItem.expirationDate, localeName)
				: "";
		} else if (index == 1) {
			var datetime = new Date();
			datetime.setHours(datetime.getHours() + 24);
			return Utils.formatDateTime(datetime, localeName);
		} else {
			return " ";
		}
		return " ";
	}



    modal: true
    visible: false

    width: 460
    height: contentItem.implicidHeight

    x: (parent.width - width) / 2
	y: (parent.height - height) / 2

    background: Rectangle {
		radius: 10
        color: Style.background_second
        anchors.fill: parent
    }

	Component.onCompleted: {
		reset();
	}

    contentItem: ColumnLayout {
        anchors.fill: parent
		anchors.margins: 30

        SFText {
			Layout.preferredWidth: parent.width
			Layout.alignment: Qt.AlignLeft
			horizontalAlignment: Text.AlignHCenter
			//: Edit addres dialog title
			//% "Edit address"
			text: qsTrId("edit-addr-title")
			color: Style.content_main
			font.pixelSize: 18
			font.weight: Font.Bold
		}

        ColumnLayout {
			Layout.preferredWidth: parent.width
			Layout.topMargin: 20
			spacing: 5

			SFText {
				Layout.preferredWidth: parent.width
				//: Edit addres dialog, address label
				//% "Address ID"
				text: qsTrId("edit-addr-addr-id")
				color: Style.content_main
				font.pixelSize: 14
				font.weight: Font.Bold
			}

			SFLabel {
				id: addressID
				Layout.preferredWidth: parent.width
				font.pixelSize: 14
				color: Style.content_secondary
                text: addressItem ? addressItem.address : ""
				elide: Text.ElideLeft
				copyMenuEnabled: true
				onCopyText: parentModel.copyToClipboard(text)
			}
    	}

//----------------------------------------------------------------------------------------------------------------------
		RowLayout {
			id: expirationForActive
			Layout.preferredWidth: parent.width
			Layout.topMargin: 20
			Layout.alignment: Qt.AlignLeft
			visible: !isExpiredAddress

			SFText {
				//: Edit addres dialog, expires label
				//% "Expires"
				text: qsTrId("edit-addr-expires-label")
				color: Style.content_main
				Layout.alignment: Qt.AlignLeft | Qt.AlignTop
				font.pixelSize: 14
				font.styleName: "Bold"; font.weight: Font.Bold
			}
			Item {
				Layout.minimumWidth: 10
			}
			ColumnLayout {
				Layout.preferredWidth: 150
				Layout.minimumWidth: 140
				Layout.maximumWidth: 150

				CustomComboBox {
					id: expirationOptionsForActive
					Layout.preferredWidth: parent.width
					Layout.alignment: Qt.AlignLeft | Qt.AlignTop
					displayText: currentText
					fontPixelSize: 14
					visible: !disactivate.checked
				}

				SFText {
					Layout.preferredWidth: parent.width
					Layout.alignment: Qt.AlignLeft | Qt.AlignTop
					//: Edit addres dialog, expire now label
					//% "now"
					text: qsTrId("edit-addr-expire-now-label")
					color: Style.content_secondary
					font.pixelSize: 14
					visible: disactivate.checked
				}

				SFText {
					id: expirationTime
					Layout.preferredWidth: parent.width
					color: Style.content_secondary
					font.pixelSize: 14
					wrapMode: Text.WordWrap
					text: getExpirationTimeLabel();
				}
			}

			Item {
				Layout.minimumWidth: 10
			}
			
			CustomSwitch {
				id: disactivate
				Layout.alignment: Qt.AlignRight | Qt.AlignTop
				//: Edit addres dialog, expire now switch
				//% "Expire address now"
				text: qsTrId("edit-addr-expire-now-switch")
				font.pixelSize: 14
				font.styleName: "Bold"
				font.weight: Font.Bold
				checked: false
			}

		}
//----------------------------------------------------------------------------------------------------------------------
		RowLayout {
			Layout.preferredWidth: parent.width
			Layout.topMargin: 15
			visible: isExpiredAddress

			SFText {
				//: Edit addres dialog, expiration time label
				//% "Expired on "
				text: qsTrId("edit-addr-expiration-time-label")
				color: Style.content_secondary
				font.pixelSize: 14
				font.italic: true
			}
			SFText {
				text: getExpirationTimeLabel()
				color: Style.content_secondary
				font.pixelSize: 14
				font.italic: true
			}
			Item {
				Layout.fillWidth: true
			}
		}

		RowLayout {
			id: expirationForUnactive
			Layout.preferredWidth: parent.width
			Layout.topMargin: 20
			Layout.alignment: Qt.AlignLeft
			visible: isExpiredAddress

			CustomSwitch {
				id: activate
				Layout.alignment: Qt.AlignLeft
				//: Edit addres dialog, expiration time label
				//% "Activate address"
				text: qsTrId("edit-addr-activate-addr-switch")
				font.pixelSize: 14
				font.styleName: "Bold"
				font.weight: Font.Bold
				checked: false
			}

			Item {
				Layout.alignment: Qt.AlignLeft
				Layout.minimumWidth: 20
			}

			SFText {
				//: Edit addres dialog, expires label
				//% "Expires"
				text: qsTrId("edit-addr-expires-label")
				color: Style.content_main
				Layout.alignment: Qt.AlignLeft
				font.pixelSize: 14
				font.styleName: "Bold"; font.weight: Font.Bold
				visible: activate.checked
			}

			Item {
				Layout.alignment: Qt.AlignLeft
				Layout.minimumWidth: 15
				visible: activate.checked
			}

			CustomComboBox {
                id: expirationOptionsForUnactive
				Layout.alignment: Qt.AlignLeft
				Layout.fillWidth: true
				Layout.minimumWidth: 125
				Layout.maximumWidth: 150
				fontPixelSize: 14
				visible: activate.checked
            }
		}
//----------------------------------------------------------------------------------------------------------------------

		ColumnLayout {
    		Layout.preferredWidth: parent.width
			Layout.topMargin: 20
			Layout.alignment: Qt.AlignLeft

			SFText {
				//: Edit addres dialog, comment label
				//% "Comment"
				text: qsTrId("edit-addr-comment")
				color: Style.content_main
				font.pixelSize: 14
				font.styleName: "Bold"; font.weight: Font.Bold
			}

			SFTextInput {
				id: addressName

				Layout.preferredWidth: parent.width
				font.pixelSize: 14
				color: Style.content_main
                text: addressItem ? addressItem.name : ""
				onTextEdited: {
					rootControl.isAddressWithCommentExist =
						parentModel.isAddressWithCommentExist(addressName.text);
				}
			}
    	}

        RowLayout {
			Layout.preferredWidth: parent.width
			Layout.topMargin: 50
			Layout.bottomMargin: 30
			Layout.alignment: Qt.AlignLeft

			Item {
				Layout.fillWidth: true
			}

			CustomButton {
				Layout.preferredHeight: 40
				//: Edit addres dialog, cancel button
				//% "cancel"
				text: qsTrId("edit-addr-cancel-button")
                icon.source: "qrc:/assets/icon-cancel.svg"
                icon.color: Style.content_main
				onClicked: {
                    rootControl.close();
                }
			}

			Item {
				Layout.minimumWidth: 30
			}

			PrimaryButton {
				id: saveButton
				Layout.preferredHeight: 40
				//: Edit addres dialog, save button
				//% "save"
				text: qsTrId("edit-addr-save-button")
                icon.source: "qrc:/assets/icon-done.svg"
                enabled: {
					if (rootControl.isExpiredAddress) {
						return (activate.checked
								|| (addressItem
									&& addressItem.name != addressName.text))
							&& !rootControl.isAddressWithCommentExist;
					} else {
						return (disactivate.checked
								&& !rootControl.isAddressWithCommentExist
								|| (expirationOptionsForActive.currentIndex != 0
									&& !isNeverExpired())
								|| (expirationOptionsForActive.currentIndex != 1
									&& isNeverExpired())
								|| (addressItem
									&& addressItem.name != addressName.text))
							&& !rootControl.isAddressWithCommentExist;
					}
                }
                onClicked: {
					var isNever = false;
					var makeActive = false;
					var makeExpired = false;

					if (rootControl.isExpiredAddress) {
						isNever =
							expirationOptionsForUnactive.currentIndex == 1;
						makeActive = activate.checked;
					}
					else {
						if (disactivate.checked) {
							makeExpired = true;
						} else if (isNeverExpired()) {
							isNever = 
								expirationOptionsForActive.currentIndex == 1;
							makeActive =
								expirationOptionsForActive.currentIndex == 0;
						} else {
							isNever =
								expirationOptionsForActive.currentIndex == 2;
							makeActive =
								expirationOptionsForActive.currentIndex == 1;
						}
					}
					parentModel.saveChanges(
							addressID.text,
							addressName.text,
							isNever,
							makeActive,
							makeExpired);
					rootControl.accepted();
                    rootControl.close();
                }
			}

			Item {
				Layout.fillWidth: true
			}
		}
    }
}
