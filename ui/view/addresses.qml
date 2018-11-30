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

            /*sortIndicatorVisible: true
            sortIndicatorColumn: 1
            sortIndicatorOrder: Qt.DescendingOrder

            Binding{
                target: viewModel
                property: "sortRole"
                value: transactionsView.getColumn(transactionsView.sortIndicatorColumn).role
            }

            Binding{
                target: viewModel
                property: "sortOrder"
                value: transactionsView.sortIndicatorOrder
            }*/
        }

        AddressTable {
            id: expiredAddressesView
            model: viewModel.expiredAddresses
            visible: false
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
                                    //txContextMenu.transaction = viewModel.transactions[styleData.row];
                                    txContextMenu.popup();
                                }
                            }
                        }
                    }
                }
            }

            ContextMenu {
                id: txContextMenu
                modal: true
                dim: false
                //property TxObject transaction
                Action {
                    text: qsTr("transactions list")
                    icon.source: "qrc:/assets/icon-transactions.svg"
                    onTriggered: {
                        /*if (!!txContextMenu.transaction)
                        {
                            viewModel.copyToClipboard(txContextMenu.transaction.user);
                        }*/
                    }
                }
                Action {
                    text: qsTr("delete")
                    icon.source: "qrc:/assets/icon-delete.svg"
                    enabled: !!txContextMenu.transaction && txContextMenu.transaction.canDelete
                    onTriggered: {
                        /*deleteTransactionDialog.text = qsTr("The transaction will be deleted. This operation can not be undone");
                        deleteTransactionDialog.open();*/
                    }
                }
                /*Connections {
                    target: deleteTransactionDialog
                    onAccepted: {
                        viewModel.deleteTx(txContextMenu.transaction);
                    }
                }*/
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
