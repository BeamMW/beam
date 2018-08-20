import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.0
import QtQuick.Controls.Styles 1.2
import "controls"

ColumnLayout {
    id: addressRoot
    width: 800
    height: 600
    anchors.fill: parent
    state: "peers"
	SFText {
        Layout.minimumHeight: 40
        Layout.maximumHeight: 40
        font.pixelSize: 36
        color: Style.white
        text: qsTr("Address book")
    }

    Dialog {
		id: confirmationDialog
        property alias text: messageText.text
        property bool isOwn

		modal: true
		width: 520
		height: 200
		x: (parent.width - width) / 2
		y: (parent.height - height) / 2
		visible: false
		
		background: Rectangle {
			radius: 10
            color: Style.dark_slate_blue
            anchors.fill: parent            
        }

        contentItem: SFText {
            anchors.centerIn: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            width: parent.width
            id: messageText
            font.pixelSize: 14
            color: Style.white
            wrapMode: Text.WordWrap
        }

        footer: DialogButtonBox {
            alignment: Qt.AlignHCenter
            spacing: 30

            background: Rectangle {
			    radius: 10
                color: Style.dark_slate_blue
                anchors.fill: parent            
            }
            
			CustomButton {
				palette.button: Style.bright_teal
				height: 38
                width: 166
				text: qsTr("delete")
				palette.buttonText: Style.marine
                DialogButtonBox.buttonRole: DialogButtonBox.AcceptRole
			}

            CustomButton {
                id: cancelButton
				palette.buttonText: Style.white
                height: 38
				width: 122
                focus: true
				text: qsTr("cancel")
                DialogButtonBox.buttonRole: DialogButtonBox.RejectRole
			}
        }

        onOpened: {
            cancelButton.forceActiveFocus(Qt.TabFocusReason);
        }
    }

	Dialog {
		id: createAddress
		modal: true
		width: 520
		height: 454
		x: (parent.width - width) / 2
		y: (parent.height - height) / 2
		visible: false
		focus: true
		background: null

		Rectangle {
			radius: 10
            color: Style.dark_slate_blue

            anchors.fill: parent

			ColumnLayout {
				id: createAddressLayout
				anchors.fill: parent
				anchors.margins: 30
				state: "peers"

				Item {
					Layout.minimumHeight: 21
					Layout.maximumHeight: 21
					Layout.fillWidth: true;

					SFText {
						anchors.centerIn: parent
						font {
							pixelSize: 18
							weight: Font.Bold
						}
						color: Style.white
						text: qsTr("Create new address")
					}
				}

				Item {
					Layout.topMargin: 30
					Layout.fillWidth: true
					Layout.minimumHeight: 20
					Layout.maximumHeight: 20

					RowLayout {
						anchors.centerIn: parent
						Layout.minimumHeight: 14
						Layout.maximumHeight: 14

						TxFilter{
							id: peersFilterDlg
							label: qsTr("PEERS ADDRESSES")
							onClicked: createAddressLayout.state = "peers"
						}

                        TxFilter{
							id: ownFilterDlg
                            Layout.leftMargin: 40
							label: qsTr("OWN ADDRESSES")
							onClicked: createAddressLayout.state = "own"
						}
					}
				}

				ColumnLayout {
					id: createPeersAddressView
					Layout.fillWidth: true
					Layout.fillHeight: true
					Layout.topMargin: 30
					visible: false
					
					SFText {
						text: qsTr("Address ID")
						Layout.fillWidth: true
                        Layout.minimumHeight: 14
						font {
							pixelSize: 12
							weight: Font.Bold
						}
						color: Style.white
					}

					SFTextInput {
						id: addressID
						Layout.fillWidth: true
						Layout.minimumHeight: 14
						Layout.maximumHeight: 14
                        focus: true
						font.pixelSize: 12
						color: Style.white
						text: addressBookViewModel.newPeerAddress.walletID
					}

					Binding {
						target: addressBookViewModel.newPeerAddress
						property: "walletID"
						value: addressID.text
					}

					Rectangle {
						Layout.fillWidth: true
                        Layout.minimumHeight: 1
						height: 1

						color: Style.separator_color
					}
						
					SFText {
                        Layout.topMargin: 30
						Layout.fillWidth: true
						Layout.fillHeight: true
                        Layout.minimumHeight: 14
						text: qsTr("Name")
						font {
							pixelSize: 12
							weight: Font.Bold
						}
						color: Style.white
					}
					SFTextInput {
						Layout.fillWidth: true
						Layout.fillHeight: true
						id: nameAddress
                        Layout.minimumHeight: 14
						Layout.maximumHeight: 14
						font.pixelSize: 12
						color: Style.white
						height: 14
						text: addressBookViewModel.newPeerAddress.name
					}
					Binding {
						target: addressBookViewModel.newPeerAddress
						property: "name"
						value: nameAddress.text
					}
					Rectangle {
						Layout.fillWidth: true
                        Layout.minimumHeight: 1
						height: 1

						color: Style.separator_color
					}
						
					SFText {
                        Layout.topMargin: 30
						Layout.fillWidth: true
						Layout.fillHeight: true
                        Layout.minimumHeight: 14
						text: qsTr("Category")
						font {
							pixelSize: 12
							weight: Font.Bold
						}
						color: Style.white
					}
					SFTextInput {
						Layout.fillWidth: true
						Layout.fillHeight: true
                        Layout.minimumHeight: 14
						Layout.maximumHeight: 14
						id: categoryAddress
						font.pixelSize: 12
						color: Style.white
						height: 14
						text: addressBookViewModel.newPeerAddress.category
					}

					Binding {
						target: addressBookViewModel.newPeerAddress
						property: "category"
						value: categoryAddress.text
					}
					Rectangle {
						Layout.fillWidth: true
                        Layout.minimumHeight: 1
						height: 1

						color: Style.separator_color
					}
				}

				ColumnLayout {
					id: createOwnAddressView
					Layout.fillWidth: true
					Layout.fillHeight: true
					Layout.topMargin: 30
					visible: false
					
					SFText {
						text: qsTr("Address ID")
						Layout.fillWidth: true
                        Layout.minimumHeight: 14
						font {
							pixelSize: 12
							weight: Font.Bold
						}
						color: Style.white
					}

					SFTextInput {
						id: ownAddressID
						Layout.fillWidth: true
                        Layout.minimumHeight: 14
						Layout.maximumHeight: 14
						font.pixelSize: 12
						color: Style.disable_text_color
						readOnly: true
                        activeFocusOnTab: false
						text: addressBookViewModel.newOwnAddress.walletID
					}

                    Item {
                        Layout.fillWidth: true
                        Layout.minimumHeight: 1
                    }

					SFText {
                        Layout.topMargin: 30
						Layout.fillWidth: true
                        Layout.minimumHeight: 14
						text: qsTr("Name")
						font {
							pixelSize: 12
							weight: Font.Bold
						}
						color: Style.white
					}
					SFTextInput {
						Layout.fillWidth: true
                        Layout.minimumHeight: 14
						Layout.maximumHeight: 14
						id: nameOwnAddress
                        focus: true
						font.pixelSize: 12
						color: Style.white
						height: 14
						text: addressBookViewModel.newOwnAddress.name
					}

					Binding {
						target: addressBookViewModel.newOwnAddress
						property: "name"
						value: nameOwnAddress.text
					}
					Rectangle {
						Layout.fillWidth: true
                        Layout.minimumHeight: 1
						height: 1
						color: Style.separator_color
					}

					SFText {
                        Layout.topMargin: 30
						Layout.fillWidth: true
                        Layout.minimumHeight: 14
						text: qsTr("Category")
						font {
							pixelSize: 12
							weight: Font.Bold
						}
						color: Style.white
					}

					SFTextInput {
						Layout.fillWidth: true
                        Layout.minimumHeight: 14
						Layout.maximumHeight: 14
						id: categoryOwnAddress
						font.pixelSize: 12
						color: Style.white
						height: 14
						text: addressBookViewModel.newOwnAddress.category
					}

					Binding {
						target: addressBookViewModel.newOwnAddress
						property: "category"
						value: categoryOwnAddress.text
					}

					Rectangle {
						Layout.fillWidth: true
                        Layout.minimumHeight: 1

						height: 1

						color: Style.separator_color
					}
				}

				Item {
					Layout.fillWidth: true
					Layout.minimumHeight: 48
					Layout.maximumHeight: 48
					Layout.topMargin: 30

					RowLayout {
						anchors.centerIn: parent
						
						CustomButton {
							palette.buttonText: Style.white
                            Layout.minimumHeight: 38
							Layout.minimumWidth: 122
							text: qsTr("cancel")
							icon.source: "qrc:///assets/icon-cancel.svg"

							onClicked: {
								createAddress.close()
							}
						}

						CustomButton {
							palette.button: Style.bright_teal
							Layout.leftMargin: 31
                            Layout.minimumHeight: 38
                            Layout.minimumWidth: 166
							text: qsTr("create address")
							icon.source: "qrc:///assets/icon-done.svg"
							palette.buttonText: Style.marine
								
							onClicked: {
								if (createAddressLayout.state == "own") {
									addressBookViewModel.createNewOwnAddress();
								} else {
									addressBookViewModel.createNewPeerAddress();
								}
								createAddress.close();
							}
						}
					}
				}

				states: [
					State {
						name: "own"
						PropertyChanges {target: ownFilterDlg; state: "active"}
						PropertyChanges {target: createOwnAddressView; visible: true}
                        PropertyChanges {target: createPeersAddressView; visible: false}

                        StateChangeScript {
                            script: {
                                nameOwnAddress.forceActiveFocus();
                            }
                        }

					},
					State {
						name: "peers"
						PropertyChanges {target: peersFilterDlg; state: "active"}
                        PropertyChanges {target: createOwnAddressView; visible: false}
						PropertyChanges {target: createPeersAddressView; visible: true}

                        StateChangeScript {
                            script: {
                                addressID.forceActiveFocus();
                            }
                        }
					}
				]
			}
		}		
	}

    RowLayout {
        Layout.fillWidth: true
        Layout.minimumHeight: 40
        Layout.maximumHeight: 40
        spacing: 40

        TxFilter{
            id: peersFilter
            Layout.leftMargin: 20
            label: qsTr("PEERS ADDRESSES")
            onClicked: addressRoot.state = "peers"
        }

        TxFilter{
            id: ownFilter
            label: qsTr("OWN ADDRESSES")
            onClicked: addressRoot.state = "own"
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            CustomButton {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                width: 195
				text: "create new address"
				palette.buttonText: Style.white
				icon.source: "qrc:///assets/icon-add.svg"
                onClicked: {
					addressBookViewModel.generateNewEmptyAddress();
					createAddressLayout.state = addressRoot.state;
					createAddress.open();
				}
            }
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true

        TableView {
            id: peersView
            anchors.fill: parent

            frameVisible: false
            selectionMode: SelectionMode.SingleSelection
            backgroundVisible: false
            model: addressBookViewModel.peerAddresses
            visible: false

            TableViewColumn {
                role: "walletID"
                title: qsTr("Address ID")
                width: 300 * parent.width / 700
                elideMode: Text.ElideMiddle
                movable: false
            }

            TableViewColumn {
                role: "name"
                title: qsTr("Name")
                width: 200 * parent.width / 700
                elideMode: Text.ElideRight
                movable: false
            }

            TableViewColumn {
                role: "category"
                title: qsTr("Category")
                elideMode: Text.ElideRight
                width: 200 * parent.width / 700
                movable: false
            }

            headerDelegate: Rectangle {
                height: 46

                color: Style.dark_slate_blue

                Text {
                    anchors.verticalCenter: parent.verticalCenter
					anchors.left: parent.left
                    anchors.leftMargin: 20
                    font.pixelSize: 12
                    color: Style.bluey_grey

                    text: styleData.value
                }
            }

            ContextMenu {
                id: peerAddressContextMenu
                property int index;

                Action {
                    text: qsTr("send money")
					icon.source: "qrc:///assets/icon-send-grey.svg"
                    onTriggered: {
                        addressBookViewModel.changeCurrentPeerAddress(peerAddressContextMenu.index);
						main.openSendDialog();
                    }
                }

				Action {
                    text: qsTr("request money")
					icon.source: "qrc:///assets/icon-recive-grey.svg"
					enabled: false
                }
				Action {
                    text: qsTr("transactions list")
					icon.source: "qrc:///assets/icon-transaction-list.svg"
					enabled: false
                }
				Action {
                    text: qsTr("copy address")
					icon.source: "qrc:///assets/icon-copy.svg"
					onTriggered: {
						if (peerAddressContextMenu.index >= 0)
						{
							var addr = addressBookViewModel.peerAddresses[peerAddressContextMenu.index];
							addressBookViewModel.copyToClipboard(addr.walletID);
						}
                    }
                }
				Action {
                    text: qsTr("edit address")
					icon.source: "qrc:///assets/icon-edit.svg"
					enabled: false
                }
				Action {
                    text: qsTr("delete address")
					icon.source: "qrc:///assets/icon-cancel.svg"
					onTriggered: {
                        var message = qsTr("The address %1 will be deleted. This operation can not be undone")
                        confirmationDialog.text = message.arg(addressBookViewModel.peerAddresses[peerAddressContextMenu.index].walletID)
                        confirmationDialog.isOwn = false
                        confirmationDialog.open();
                        console.log("after close dialog");
                    }                    
                }
                Connections {
                    target: confirmationDialog
                    onAccepted: {
                        if (!confirmationDialog.isOwn)
                            addressBookViewModel.deletePeerAddress(peerAddressContextMenu.index);
                    }
                }
            }

			ContextMenu {
                id: ownAddressContextMenu
                property int index;
				Action {
                    text: qsTr("copy address")
					icon.source: "qrc:///assets/icon-copy.svg"
					onTriggered: {
						if (ownAddressContextMenu.index >= 0)
						{
							var addr = addressBookViewModel.ownAddresses[ownAddressContextMenu.index];
							addressBookViewModel.copyToClipboard(addr.walletID);
						}
                    }
                }
				Action {
                    text: qsTr("delete address")
					icon.source: "qrc:///assets/icon-cancel.svg"
					onTriggered: {
                        var message = qsTr("The address %1 will be deleted. This operation can not be undone")
                        confirmationDialog.text = message.arg(addressBookViewModel.ownAddresses[ownAddressContextMenu.index].walletID)
                        confirmationDialog.isOwn = true
                        confirmationDialog.open();
                    }
                    // User shouldn't delete "default" or last own adress
                    enabled: ((ownAddressesView.rowCount > 1) && 
                              (addressBookViewModel.ownAddresses[ownAddressContextMenu.index].name !== "default"))
                }
                Connections {
                    target: confirmationDialog
                    onAccepted: {
                        if (confirmationDialog.isOwn)
                            addressBookViewModel.deleteOwnAddress(ownAddressContextMenu.index);
                    }
                }
            }

            rowDelegate: Item {

                height: 69

                anchors.left: parent.left
                anchors.right: parent.right

                Rectangle {
                    anchors.fill: parent

                    color: styleData.selected ? Style.bright_sky_blue : Style.light_navy
                    visible: styleData.selected ? true : styleData.alternate
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: {
                        if (mouse.button === Qt.RightButton && styleData.row !== undefined)
                        {
                            peerAddressContextMenu.index = styleData.row;
                            peerAddressContextMenu.popup();
                        }
                    }
                }

            }

            itemDelegate: TableItem {
                text: styleData.value
                elide: styleData.elideMode
            }
        }

		TableView {
            id: ownAddressesView
            anchors.fill: parent

            frameVisible: false
            selectionMode: SelectionMode.NoSelection
            backgroundVisible: false
            model: addressBookViewModel.ownAddresses

            TableViewColumn {
                role: "walletID"
                title: qsTr("Address ID")
                width: 300 * parent.width / 850
                elideMode: Text.ElideMiddle
                movable: false
            }

            TableViewColumn {
                role: "name"
                title: qsTr("Name")
                width: 200 * parent.width / 850
                elideMode: Text.ElideRight
                movable: false
            }

            TableViewColumn {
                role: "category"
                title: qsTr("Category")
                elideMode: Text.ElideRight
                width: 200 * parent.width / 850
                movable: false
            }

//            TableViewColumn {
//                role: "expirationDate"
//                title: qsTr("Expiration date")
//                movable: false
//            }

            TableViewColumn {
                role: "createDate"
                title: qsTr("Created")
                elideMode: Text.ElideRight
                width: 150 * parent.width / 850
                movable: false
            }


            headerDelegate: Rectangle {
                height: 46

                color: Style.dark_slate_blue

                Text {
                    anchors.verticalCenter: parent.verticalCenter
					anchors.left: parent.left
                    anchors.leftMargin: 20
                    font.pixelSize: 12
                    color: Style.bluey_grey

                    text: styleData.value
                }
            }

            rowDelegate: Item {

                height: 69

                anchors.left: parent.left
                anchors.right: parent.right

                Rectangle {
                    anchors.fill: parent

                    color: styleData.selected ? Style.bright_sky_blue : Style.light_navy
                    visible: styleData.selected ? true : styleData.alternate
                }

				MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: {
                        if (mouse.button === Qt.RightButton && styleData.row !== undefined)
                        {
                            ownAddressContextMenu.index = styleData.row;
                            ownAddressContextMenu.popup();
                        }
                    }
                }
            }

            itemDelegate: TableItem {
                text: styleData.value
                elide: styleData.elideMode
            }
        }
    }

    states: [
        State {
            name: "own"
            PropertyChanges {target: ownFilter; state: "active"}
            PropertyChanges {
                target: ownAddressesView
                visible: true
            }

            PropertyChanges {
                target: peersView
                visible: false
            }
        },
        State {
            name: "peers"
            PropertyChanges {target: peersFilter; state: "active"}

            PropertyChanges {
                target: ownAddressesView
                visible: false
            }

            PropertyChanges {
                target: peersView
                visible: true
            }
        }
    ]

}
