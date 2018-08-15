import QtQuick 2.6
import QtQuick.Controls 1.2
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
import QtQuick.Layouts 1.3
import "controls"

Item {
    id: root
    anchors.fill: parent

    property bool toSend: false

    state: "wallet"

    SFText {
        font.pixelSize: 36
        color: Style.white
        text: "Wallet"
    }

    Rectangle {
        id: user_led
        y: 55

        width: 10
        height: 10

        radius: 5

        color: Style.bright_teal
    }

    DropShadow {
        anchors.fill: user_led
        radius: 5
        samples: 9
        color: Style.bright_teal
        source: user_led
    }

    SFText {
		id: linkBtn
        x: 20
        y: 53

        font.pixelSize: 12
        color: Style.bluey_grey
        linkColor: Style.white

        text: {
            if(walletViewModel.syncProgress < 0)
                "Last update time: " + walletViewModel.syncTime + " (<a href=\"update\">update</a>)"
            else
                "Updating, please wait... [" + walletViewModel.syncProgress + "%]"
        }

        onLinkActivated: {
			if(link == "update")
			{
				walletViewModel.syncWithNode()
			}
		}

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            cursorShape: parent.hoveredLink ? Qt.PointingHandCursor : Qt.ArrowCursor
        }
    }

    /////////////////////////////////////////////////////////////
    /// Send layout /////////////////////////////////////////////
    /////////////////////////////////////////////////////////////

    Rectangle {
        id: send_layout
        anchors.fill: parent
        anchors.topMargin: 97
        anchors.bottomMargin: 30

        radius: 10
        color: Style.dark_slate_blue

        ColumnLayout {
            anchors.fill: parent
            anchors.topMargin: 30
            anchors.bottomMargin: 30
            anchors.leftMargin: 30
            anchors.rightMargin: 30

            clip: true

			RowLayout {
				Layout.fillWidth: true
		        //Layout.fillHeight: true
				height: 300

				spacing: 30

				Item {
					Layout.fillWidth: true
				    Layout.fillHeight: true
                
					clip: true

                    SFText {
                        font.pixelSize: 18
                        font.weight: Font.Bold
                        color: Style.white
                        text: qsTr("Send BEAM")
                    }

					ColumnLayout {
						anchors.fill: parent
						Layout.rightMargin: 30
						clip: true						                        

						SFText {
                            Layout.topMargin: 71
							Layout.minimumHeight: 14
							Layout.maximumHeight: 14                           

                            font.pixelSize: 12
                            font.weight: Font.Bold
                            color: Style.white
                            text: qsTr("My address")
                        }                    

					    AddressComboBox {
						    id: senderAddrCombo
						    Layout.fillWidth: true
						    Layout.minimumHeight: 18
						    Layout.maximumHeight: 18
						    editable: true
						    model: addressBookViewModel.ownAddresses
						    currentIndex: -1
                            color: Style.white
						    font.pixelSize: 12
                            onEditTextChanged: {
                                var i = find(editText);
                                senderName.text = i >= 0 ? addressBookViewModel.ownAddresses[i].name : "";
                            }
						} 
                        SFText {
                            Layout.topMargin: 10
                            id: senderName
						    Layout.fillWidth: true
						    Layout.minimumHeight: 18
                            color: Style.white
                        }
                        
                        SFText {							
							Layout.minimumHeight: 14
							Layout.maximumHeight: 14
                            Layout.topMargin: 30

                            font.pixelSize: 12
                            font.weight: Font.Bold
                            color: Style.white
                            text: qsTr("Peer address")
                        }

					    AddressComboBox {
						    id: receiverAddrCombo
						    Layout.fillWidth: true
						    Layout.minimumHeight: 18
						    Layout.maximumHeight: 18
						    editable: true
						    currentIndex: -1
						    model: addressBookViewModel.peerAddresses
                            color: Style.white
							font.pixelSize: 12
                            onEditTextChanged: {
                                var i = find(editText);
                                receiverName.text = i >= 0 ? addressBookViewModel.peerAddresses[i].name : "";
                            }
					    }

                        SFText {
                            Layout.topMargin: 10
                            id: receiverName
						    Layout.fillWidth: true
						    Layout.minimumHeight: 18
                            color: Style.white
                        }

                        Binding {
                             target: walletViewModel
                             property: "senderAddr"
                             value: senderAddrCombo.editText
                        }

					    Binding {
                             target: senderAddrCombo
                             property: "editText"
                             value: walletViewModel.senderAddr
                        }

					    Binding {
                             target: walletViewModel
                             property: "receiverAddr"
                             value: receiverAddrCombo.editText
                        }

					    Binding {
                             target: receiverAddrCombo
                             property: "editText"
                             value: walletViewModel.receiverAddr
                        }

						Item {
							Layout.fillHeight: true;
						}
					}
				}

				Item {
					id: sendLeftPanel
					Layout.fillWidth: true
				    Layout.fillHeight: true

					SFText {
						id: amount_text
						y: 41

						font.pixelSize: 12
						font.weight: Font.Bold
						color: Style.white
						text: "Transaction amount"
					}

					SFTextInput {
						id: amount_input
						y: 93-30
						width: 300

						font.pixelSize: 48
						color: Style.heliotrope

						text: walletViewModel.sendAmount

						validator: IntValidator{bottom: 0; top: 210000000;}
						selectByMouse: true
					}

					Binding {
						target: walletViewModel
						property: "sendAmount"
						value: amount_input.text
					}

					Rectangle {
						y: 153-30
						width: 337
						height: 1

						color: Style.separator_color
					}

					SFText {
						x: 204+140
						y: 117-30

						font.pixelSize: 24
						color: Style.white
						text: "BEAM"
					}

					SFTextInput {
						id: mils_amount_input
						y: 93+30
						width: 300

						font.pixelSize: 48

						color: Style.heliotrope

						text: walletViewModel.sendAmountMils

						validator: IntValidator{bottom: 0; top: 999999;}
					}

					Binding {
						target: walletViewModel
						property: "sendAmountMils"
						value: mils_amount_input.text
					}

					Rectangle {
						y: 153+30
						width: 337
						height: 1

						color: Style.separator_color
					}

					SFText {
						x: 204+140
						y: 117+30

						font.pixelSize: 24
						color: Style.white
						text: qsTr("GROTH")
					}

					/////////////////////////////////////////////////////////////
					/// Transaction fee /////////////////////////////////////////
					/////////////////////////////////////////////////////////////

					Column {
						id: fee_input
						anchors {
							top: mils_amount_input.bottom
							left: amount_text.left
							right: parent.right
							topMargin: 20
						}

						SFText {

							font.pixelSize: 12
							font.weight: Font.Bold
							color: Style.white
							text: "Transaction fee"
						}

						RowLayout {
							anchors {
								left: parent.left
							}
							SFTextInput {
								id: mils_fee_input
								Layout.minimumWidth: 337
								Layout.maximumWidth: 337

								font.pixelSize: 48

								color: Style.heliotrope

								text: walletViewModel.feeMils

								validator: IntValidator{bottom: 0; top: 999999;}
							}
							SFText {
								Layout.alignment: Qt.AlignBottom
								Layout.leftMargin: 0
								font.pixelSize: 24
								color: Style.white
								text: qsTr("GROTH")
							}
						}

						Rectangle {
							width: 337
							height: 1

							color: Style.separator_color
						}

						Binding {
							target: walletViewModel
							property: "feeMils"
							value: mils_fee_input.text
						}
					}
					Item {
						id: total_amount
						anchors {
							top: fee_input.bottom
							left: parent.left
							right: parent.right
							topMargin: 20
							rightMargin: 30
						}
						height: 40

						SFText {
							opacity: 0.5
							font.pixelSize: 24
							font.weight: Font.ExtraLight
							color: Style.white
							text: (walletViewModel.sendAmount*1 + (walletViewModel.sendAmountMils*1 + walletViewModel.feeMils*1)/1000000) + " BEAM"
						}
					}
				}
			}

			RowLayout {
				Layout.fillWidth: true
				Layout.minimumHeight: 80
				Item {
					Layout.fillWidth: true
					Layout.fillHeight: true
				}
				AvailablePanel {
				    color: "transparent"
					Layout.fillWidth: true
					Layout.fillHeight: true
					Layout.leftMargin:-27
					Layout.topMargin: -15
					value: walletViewModel.actualAvailable
				}
			}
			

            Row {
				Layout.alignment: Qt.AlignCenter 
				Layout.fillWidth: false
				Layout.fillHeight: true
				height: 40

                spacing: 30

                Rectangle {
                    width: 130
                    height: 40

                    radius: 20

                    color: Style.separator_color

                    SFText {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: 18
                        font.weight: Font.Bold
                        color: Style.white
                        text: "CANCEL"

                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.state = "wallet"
                    }
                }

                Rectangle {
                    width: 108
                    height: 40

                    radius: 20

                    color: Style.heliotrope

                    SFText {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: 18
                        font.weight: Font.Bold
                        color: Style.white
                        text: "SEND"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            walletViewModel.sendMoney()
                            root.state = "wallet"
                        }
                    }
                }                
            }
        }

        visible: false
    }

    Item {

        id: wallet_layout
        anchors.fill: parent
        state: "wide"

        CuteButton {
            anchors.top: parent.top
            anchors.right: parent.right

            label: "SEND"

            onClicked: root.state = "send"
        }

        Item {
            y: 97
            height: 206

            anchors.left: parent.left
            anchors.right: parent.right

            RowLayout {

                id: wide_panels

                anchors.left: parent.left
                anchors.right: parent.right
                height: parent.height

                spacing: 30

                AvailablePanel {
                    Layout.maximumWidth: 500
                    Layout.minimumWidth: 350
                    Layout.fillHeight: true
                    Layout.fillWidth: true
					
                    value: walletViewModel.available
                }

                SecondaryPanel {
                    Layout.minimumWidth: 350
                    Layout.fillHeight: true
					Layout.fillWidth: true
                    title: qsTr("Unconfirmed")
                    amountColor: Style.white
                    value: walletViewModel.unconfirmed
                }
            }
        }

        Item
        {
            y: 320

            anchors.left: parent.left
            anchors.right: parent.right

            SFText {
                x: 30

                font {
                    pixelSize: 18
                    weight: Font.Bold
                }

                color: Style.white

                text: "Transactions"
            }

//            Row {
//
//                anchors.right: parent.right
//                spacing: 20
//                state: "all"
//
//                TxFilter{
//                    id: all
//                    label: "ALL"
//                    onClicked: parent.state = "all"
//                }
//
//                TxFilter{
//                    id: sent
//                    label: "SENT"
//                    onClicked: parent.state = "sent"
//                }
//
//                TxFilter{
//                    id: received
//                    label: "RECEIVED"
//                    onClicked: parent.state = "received"
//                }
//
//                TxFilter{
//                    id: in_progress
//                    label: "IN PROGRESS"
//                    onClicked: parent.state = "in_progress"
//                }
//
//                states: [
//                    State {
//                        name: "all"
//                        PropertyChanges {target: all; state: "active"}
//                    },
//                    State {
//                        name: "sent"
//                        PropertyChanges {target: sent; state: "active"}
//                    },
//                    State {
//                        name: "received"
//                        PropertyChanges {target: received; state: "active"}
//                    },
//                    State {
//                        name: "in_progress"
//                        PropertyChanges {target: in_progress; state: "active"}
//                    }
//                ]
//            }
        }

	    Rectangle {
            anchors.fill: parent;
            anchors.topMargin: 394+46-33

            color: "#0a344d"
        }

        TableView {

            id: tx_view

            anchors.fill: parent;
            anchors.topMargin: 394-33

            frameVisible: false
            selectionMode: SelectionMode.NoSelection
            backgroundVisible: false

            TableViewColumn {
                role: "income"
                width: 72*parent.width/1310
                elideMode: Text.ElideRight
                movable: false

                delegate: Item {

                    anchors.fill: parent

                    clip:true

                    SvgImage {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.horizontalCenter: parent.horizontalCenter
                        source: styleData.value ? "qrc:///assets/icon-received.svg" : "qrc:///assets/icon-sent.svg"
                    }
                }
            }

            TableViewColumn {
                role: "date"
                title: "Date | Time"
                width: 200*parent.width/1310
                elideMode: Text.ElideRight

                movable: false
            }

            TableViewColumn {
                role: "user"
                title: "Recipient / Sender ID"
                width: 200*parent.width/1310
                elideMode: Text.ElideMiddle

                movable: false
            }

            TableViewColumn {
                role: "comment"
                title: "Comment"
                width: 120*parent.width/1310
                elideMode: Text.ElideRight
                movable: false

                delegate: Item {

                    anchors.fill: parent

                    clip:true

                    SvgImage {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 20
                        source: "qrc:///assets/icon-comment.svg"
                        visible: styleData.value !== null
                    }
                }
            }

            TableViewColumn {
                role: "amount"
                title: "Amount, BEAM"
                width: 200*parent.width/1310
                elideMode: Text.ElideRight
                movable: false

                delegate: Row {
                    anchors.fill: parent
                    spacing: 6

                    clip:true

                    property bool income: (styleData.row >= 0) ? walletViewModel.tx[styleData.row].income : false

                    SFText {
                        font.pixelSize: 24

                        color: parent.income ? Style.bright_sky_blue : Style.heliotrope

                        anchors.verticalCenter: parent.verticalCenter
                        text: (parent.income ? "+ " : "- ") + styleData.value
                    }

                    SFText {
                        font.pixelSize: 12

                        color: parent.income ? Style.bright_sky_blue : Style.heliotrope

                        anchors.verticalCenter: parent.verticalCenter
                        anchors.verticalCenterOffset: 4
                        text: "BEAM"
                    }
                }
            }

            TableViewColumn {
                role: "change"
                title: "Change, BEAM"
                width: 200*parent.width/1310
                elideMode: Text.ElideRight

                movable: false
            }

            TableViewColumn {
                role: "status"
                title: "Status"
                width: 96*parent.width/1310
                elideMode: Text.ElideRight
                movable: false

                delegate: Item {

                    anchors.fill: parent

                    clip:true

                    SFText {
                        font.pixelSize: 12

                        color: {
                            if(styleData.value === "sent")
                                Style.heliotrope
                            else if(styleData.value === "received")
                                Style.bright_sky_blue
                            else Style.white
                        }
                        elide: Text.ElideRight
                        anchors.verticalCenter: parent.verticalCenter
                        text: styleData.value
                    }
                }
            }

            model: walletViewModel.tx

            headerDelegate: Rectangle {
                height: 46

                color: Style.dark_slate_blue

                SFText {
                    anchors.verticalCenter: parent.verticalCenter

                    font.pixelSize: 12
                    color: Style.bluey_grey

                    text: styleData.value
                }
            }

            ContextMenu {
                id: txContextMenu
                property int txIndex;
                Action {
                    text: qsTr("cancel")
                    onTriggered: {
                       walletViewModel.cancelTx(txContextMenu.txIndex);
                    }
					icon.source: "qrc:///assets/icon-cancel.svg"
                }
            }

            rowDelegate: Item {
                height: 69

                anchors.left: parent.left
                anchors.right: parent.right

                Rectangle {
                    anchors.fill: parent

                    color: Style.light_navy
                    visible: styleData.alternate
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: {
                        if (mouse.button === Qt.RightButton && styleData.row !== undefined && styleData.row >=0)
                        {
                            txContextMenu.txIndex = styleData.row;
                            var tx = walletViewModel.tx[styleData.row];
                            if (tx.canCancel)
                            {
                                txContextMenu.popup();
                            }
                        }
                    }
                }
            }

            itemDelegate: TableItem {
                text: styleData.value
                elide: styleData.elideMode
            }
        }
        
        states: [
            State {
                name: "wide"
            },

            State {
                when: wallet_layout.visible && wallet_layout.width < (1440-70-2*30)
                name: "medium"
                // PropertyChanges {target: wide_panels; visible: false}
                // PropertyChanges {target: medium_panels; visible: true}
            },

            State {
                when: wallet_layout.visible && wallet_layout.width < (1440-70-2*30)
                name: "small"
                // PropertyChanges {target: wide_panels; visible: false}
                // PropertyChanges {target: medium_panels; visible: true}
            }
        ]
    }
    
    Component.onCompleted: {
        if (root.toSend) {
            root.state = "send"
            root.toSend = false
        }
    }

    states: [
        State {
            name: "wallet"
        },

        State {
            name: "send"
            PropertyChanges {target: wallet_layout; visible: false}
            PropertyChanges {target: send_layout; visible: true}
        }
    ]
}
