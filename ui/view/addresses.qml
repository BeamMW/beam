import QtQuick 2.11
import QtQuick.Controls 1.4
import QtQuick.Controls 2.3
import QtQuick.Layouts 1.0
import QtQuick.Controls.Styles 1.2
import "controls"
import Beam.Wallet 1.0

ColumnLayout {
    id: addressRoot

	AddressBookViewModel {id: viewModel}

    anchors.fill: parent
    state: "active"
	SFText {
        Layout.minimumHeight: 40
        Layout.maximumHeight: 40
        font.pixelSize: 36
        color: Style.white
        text: qsTr("Addresses")
    }

    ConfirmationDialog {
		id: confirmationDialog
        property bool isOwn
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.minimumHeight: 40
        Layout.maximumHeight: 40
        spacing: 40

        TxFilter{
            id: activeAddressesFilter
            Layout.leftMargin: 20
            label: qsTr("MY ACTIVE ADDRESSES")
            onClicked: addressRoot.state = "active"
        }

        TxFilter{
            id: expiredAddressesFilter
            label: qsTr("MY EXPIRED ADDRESSES")
            onClicked: addressRoot.state = "expired"
        }

        TxFilter{
            id: contactsFilter
            label: qsTr("CONTACTS")
            onClicked: addressRoot.state = "contacts"
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
        }
    }

    Item {
        Layout.fillWidth: true
        Layout.fillHeight: true


        AddressTable {
            id: activeAddressesView
            model: viewModel.activeAddresses
            visible: false

            sortIndicatorVisible: true
            sortIndicatorColumn: 4
            sortIndicatorOrder: Qt.DescendingOrder

            Binding{
                target: viewModel
                property: "activeAddrSortRole"
                value: activeAddressesView.getColumn(activeAddressesView.sortIndicatorColumn).role
            }

            Binding{
                target: viewModel
                property: "activeAddrSortOrder"
                value: activeAddressesView.sortIndicatorOrder
            }
        }

        AddressTable {
            id: expiredAddressesView
            model: viewModel.expiredAddresses
            visible: false

            sortIndicatorVisible: true
            sortIndicatorColumn: 4
            sortIndicatorOrder: Qt.DescendingOrder

            Binding{
                target: viewModel
                property: "expiredAddrSortRole"
                value: expiredAddressesView.getColumn(expiredAddressesView.sortIndicatorColumn).role
            }

            Binding{
                target: viewModel
                property: "expiredAddrSortOrder"
                value: expiredAddressesView.sortIndicatorOrder
            }
        }
        
        CustomTableView {
            id: contactsView

            property int rowHeight: 69
            property int resizableWidth: parent.width - actions.width

            anchors.fill: parent
            frameVisible: false
            selectionMode: SelectionMode.NoSelection
            backgroundVisible: false
            model: viewModel.contacts

            TableViewColumn {
                role: viewModel.nameRole
                title: qsTr("Name")
                width: 280 * contactsView.resizableWidth / 740
                movable: false
            }

            TableViewColumn {
                role: viewModel.addressRole
                title: qsTr("Contact")
                width: 170 * contactsView.resizableWidth / 740
                movable: false
                delegate: Item {
                    Item {
                        width: parent.width
                        height: contactsView.rowHeight
                        clip:true

                        SFLabel {
                            font.pixelSize: 14
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: 20
                            elide: Text.ElideMiddle
                            anchors.verticalCenter: parent.verticalCenter
                            text: styleData.value
                            color: Style.white
                            copyMenuEnabled: true
                            onCopyText: viewModel.copyToClipboard(text)
                        }
                    }
                }
            }

            TableViewColumn {
                role: viewModel.categoryRole
                title: qsTr("Category")
                width: 290 * contactsView.resizableWidth / 740
                movable: false
            }

            TableViewColumn {
                //role: "status"
                id: actions
                title: ""
                width: 40
                movable: false
                resizable: false
                delegate: txActions
            }

            rowDelegate: Item {

                height: contactsView.rowHeight

                anchors.left: parent.left
                anchors.right: parent.right

                Rectangle {
                    anchors.fill: parent

                    color: styleData.selected ? Style.bright_sky_blue : Style.light_navy
                    visible: styleData.selected ? true : styleData.alternate
                }
            }

            itemDelegate: TableItem {
                text: styleData.value
                elide: styleData.elideMode
            }

            Component {
                id: txActions
                Item {
                    Item {
                        width: parent.width
                        height: contactsView.rowHeight

                        Row{
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 10
                            CustomToolButton {
                                icon.source: "qrc:/assets/icon-actions.svg"
                                ToolTip.text: qsTr("Actions")
                                onClicked: {
                                    contextMenu.address = contactsView.model[styleData.row].address;
                                    contextMenu.popup();
                                }
                            }
                        }
                    }
                }
            }

            ContextMenu {
                id: contextMenu
                modal: true
                dim: false
                property string address
                /*Action {
                    text: qsTr("transactions list")
                    icon.source: "qrc:/assets/icon-transactions.svg"
                    onTriggered: {
                        // go to list transaction (wallet page)
                        main.updateItem(0)
                    }
                }*/
                Action {
                    text: qsTr("delete contact")
                    icon.source: "qrc:/assets/icon-delete.svg"
                    onTriggered: {
                        viewModel.deleteAddress(contextMenu.address);
                    }
                }
            }
        }
    }

    states: [
        State {
            name: "active"
            PropertyChanges {target: activeAddressesFilter; state: "active"}
            PropertyChanges {
                target: activeAddressesView
                visible: true
            }
            PropertyChanges {
                target: expiredAddressesView
                visible: false
            }
            PropertyChanges {
                target: contactsView
                visible: false
            }
        },
        State {
            name: "expired"
            PropertyChanges {target: expiredAddressesFilter; state: "active"}
            PropertyChanges {
                target: activeAddressesView
                visible: false
            }
            PropertyChanges {
                target: expiredAddressesView
                visible: true
            }
            PropertyChanges {
                target: contactsView
                visible: false
            }
        },
        State {
            name: "contacts"
            PropertyChanges {target: contactsFilter; state: "active"}
            PropertyChanges {
                target: activeAddressesView
                visible: false
            }
            PropertyChanges {
                target: expiredAddressesView
                visible: false
            }
            PropertyChanges {
                target: contactsView
                visible: true
            }
        }
    ]

}
