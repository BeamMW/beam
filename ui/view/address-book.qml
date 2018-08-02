import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Controls 2.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Styles 1.2
import "controls"

ColumnLayout {
    id: addressRoot
    width: 800
    height: 600
    anchors.fill: parent
    state: "own"
	SFText {
        Layout.minimumHeight: 40
        Layout.maximumHeight: 40
        font.pixelSize: 36
        color: Style.white
        text: qsTr("Address book")
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.minimumHeight: 40
        Layout.maximumHeight: 40
        spacing: 40
        TxFilter{
            id: ownFilter
            Layout.leftMargin: 20
            label: qsTr("OWN ADDRESSES")
            onClicked: addressRoot.state = "own"
        }

        TxFilter{
            id: peersFilter
            label: qsTr("PEERS ADDRESSES")
            onClicked: addressRoot.state = "peers"
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Button {
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                text: "+ create new address"
				onClicked: {
                    addressBookViewModel.createNewAddress();
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
                width: 300

                movable: false
            }

            TableViewColumn {
                role: "name"
                title: qsTr("Name")
                width: 200

                movable: false
            }

            TableViewColumn {
                role: "category"
                title: qsTr("Category")
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

                    color: Style.light_navy
                    visible: styleData.alternate
                }

            }

            itemDelegate: TableItem {
                text: styleData.value
            }
        }

		TableView {
            id: ownAddressesView
            anchors.fill: parent

            frameVisible: false
            selectionMode: SelectionMode.SingleSelection
            backgroundVisible: false
            model: addressBookViewModel.ownAddresses

            TableViewColumn {
                role: "walletID"
                title: qsTr("Address ID")
                width: 300

                movable: false
            }

            TableViewColumn {
                role: "name"
                title: qsTr("Name")
                width: 200

                movable: false
            }

            TableViewColumn {
                role: "category"
                title: qsTr("Category")
                movable: false
            }

            TableViewColumn {
                role: "expirationDate"
                title: qsTr("Expiration date")
                movable: false
            }

            TableViewColumn {
                role: "createDate"
                title: qsTr("Created")
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

                    color: Style.light_navy
                    visible: styleData.alternate
                }
            }

            itemDelegate: TableItem {
                text: styleData.value
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
