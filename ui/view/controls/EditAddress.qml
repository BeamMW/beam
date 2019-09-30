import QtQuick 2.11
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.11
import Beam.Wallet 1.0
import "."
import "../utils.js" as Utils

Dialog {
	id: rootControl

    property var parentModel
	property var expirationOptions: [
		//: Edit address dialog, expiration option, in 24 hours from now
		//% "In 24 hours from now"
		qsTrId("edit-addr-24-option"),
		//: Edit address dialog, expiration option, never
		//% "Never"
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
		isAddressWithCommentExist = false;
		if (isExpiredAddress) {
			expirationOptionsForUnactive.model = expirationOptions;
			expirationOptionsForUnactive.currentIndex = 0;
		} else {
			if (isNeverExpired()) {
				expirationOptionsForActive.model = expirationOptions;
				expirationOptionsForActive.currentIndex = 1;
			} else {
				//: Edit address dialog, expiration option, do not change
				//% "Within 24 hours"
				expirationOptionsForActive.model = [qsTrId("edit-addr-as-is-option"),].concat(expirationOptions);
			}
		}
	}

	property var getExpirationTimeLabel: function() {
		var localeName = BeamGlobals.getLocaleName();
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
	property int kPreferredWidth: 450

    modal: true
    visible: false

	height: 400

    x: (parent.width - width) / 2
	y: (parent.height - height) / 2

    background: Rectangle {
		radius: 10
        color: Style.background_popup
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
				onCopyText: BeamGlobals.copyToClipboard(text)
			}
    	}

//----------------------------------------------------------------------------------------------------------------------
		RowLayout {
			id: expirationForActive
			TextMetrics {
				id: textMeterActiveAddr
				font {
					family: "SF Pro Display"
					styleName: "Bold"
					weight: Font.Bold
					pixelSize: 14
					}
				text: expiresDateLabel.text + " " + disactivate.text
			}
			Layout.preferredWidth: {
				return Math.max(
					kPreferredWidth,
					parseInt(textMeterActiveAddr.width) + 45 + 35 + 160 + 30);
			}
			Layout.topMargin: 20
			Layout.alignment: Qt.AlignLeft
			visible: !isExpiredAddress

			SFText {
				id: expiresDateLabel
				//: Edit addres dialog, expires label
				//% "Expires"
				text: qsTrId("edit-addr-expires-label")
				color: Style.content_main
				Layout.alignment: Qt.AlignLeft | Qt.AlignTop
				font.pixelSize: 14
				font.styleName: "Bold"; font.weight: Font.Bold
			}
			Item {
				Layout.preferredWidth: 10
			}
			ColumnLayout {
				Layout.preferredWidth: 160
				Layout.minimumWidth: 140
				Layout.maximumWidth: 160

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
					//% "Now"
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
				Layout.preferredWidth: 10
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
			Layout.fillWidth: true
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
			TextMetrics {
				id: textMeterUnactiveAddr
				font {
					family: "SF Pro Display"
					styleName: "Bold"
					weight: Font.Bold
					pixelSize: 14
					}
				text: activate.text + " " + expiresLabel.text
			}
			Layout.preferredWidth: {
				return Math.max(
					kPreferredWidth,
					parseInt(textMeterUnactiveAddr.width) + 45 + 35 + 150 + 30);
			}
			id: expirationForUnactive
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
				Layout.preferredWidth: 20
			}

			SFText {
				id: expiresLabel
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
				Layout.preferredWidth: 15
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
				//% "Comment"
				text: qsTrId("general-comment")
				color: Style.content_main
				font.pixelSize: 14
				font.styleName: "Bold"; font.weight: Font.Bold
			}

			SFTextInput {
				id: addressName

				Layout.preferredWidth: parent.width
				font.pixelSize: 14
				font.italic : rootControl.isAddressWithCommentExist
				backgroundColor: rootControl.isAddressWithCommentExist ? Style.validator_error : Style.content_main
				color: rootControl.isAddressWithCommentExist ? Style.validator_error : Style.content_main
                text: addressItem ? addressItem.name : ""
				onTextEdited: {
					rootControl.isAddressWithCommentExist =
						parentModel.isAddressWithCommentExist(addressName.text);
				}
			}

			Item {
				Layout.preferredHeight: 15
				SFText {
					//% "Address with the same comment already exists"
					text: qsTrId("general-addr-comment-error")
					color: Style.validator_error
					font.pixelSize: 12
					visible: rootControl.isAddressWithCommentExist
				}
			}
    	}

        RowLayout {
			Layout.preferredWidth: parent.width
			Layout.topMargin: 35
			Layout.bottomMargin: 30
			Layout.alignment: Qt.AlignLeft

			Item {
				Layout.fillWidth: true
			}

			CustomButton {
				Layout.preferredHeight: 40
				//: Edit addres dialog, cancel button
				//% "Cancel"
				text: qsTrId("general-cancel")
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
				//% "Save"
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
					var expirationStatus;
					const expirationStatusEnum = {
						Expired: 0,
						OneDay: 1,
						Never: 2,
						AsIs: 3
					}

					if (rootControl.isExpiredAddress) {
						if (activate.checked) {
							switch(expirationOptionsForUnactive.currentIndex) {
								case 0:
									expirationStatus = expirationStatusEnum.OneDay;
									break;
								case 1:
									expirationStatus = expirationStatusEnum.Never;
									break;
							}
						}
					}
					else {
						if (disactivate.checked) {
							expirationStatus = expirationStatusEnum.Expired;
						} else if (isNeverExpired()) {
							switch(expirationOptionsForActive.currentIndex) {
								case 0:
									expirationStatus = expirationStatusEnum.OneDay;
									break;
								case 1:
									expirationStatus = expirationStatusEnum.Never;
									break;
							}
						} else {
							switch(expirationOptionsForActive.currentIndex) {
								case 1:
									expirationStatus = expirationStatusEnum.OneDay;
									break;
								case 2:
									expirationStatus = expirationStatusEnum.Never;
									break;
								default:
									expirationStatus = expirationStatusEnum.AsIs;
							}
						}
					}
					console.log(expirationStatus);
					parentModel.saveChanges(addressID.text, addressName.text, expirationStatus);
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
