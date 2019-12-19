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

    EditAddress {
        id: editActiveAddress
        parentModel: viewModel
    }

    EditAddress {
        id: editExpiredAddress
        parentModel: viewModel
        isExpiredAddress: true
    }

    anchors.fill: parent
    state: "active"
	Title {
        //% "Addresses"
        text: qsTrId("addresses-tittle")
    }

    StatusBar {
        id: status_bar
        model: statusbarModel
    }

    ConfirmationDialog {
		id: confirmationDialog
        property bool isOwn
    }

    Dialog {
        id: showQR
        property var addressItem: null
        modal: true
        width: 462
        height: 541

        x: (parent.width - width) / 2
        y: (parent.height - height) / 2
        visible: false
        
        background: Rectangle {
            radius: 10
            color: Style.background_popup
            anchors.fill: parent
        }

        contentItem: ColumnLayout {
            anchors.fill: parent
            anchors.margins: 30
            spacing: 0

            Item {
                width: parent.width
                height: 29

                SFText {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    //: show QR dialog title
                    //% "QR code"
                    text: qsTrId("show-qr-title")
                    color: Style.content_main
                    font.pixelSize: 24
                    font.styleName: "Bold"; font.weight: Font.Bold
                }
                Image {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    fillMode: Image.Pad     
                    source: "qrc:/assets/icon-cancel-16.svg"
                    MouseArea {
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            showQR.close();
                        }
                    }
                }
            }

            Item {
                Layout.fillHeight: true
                Layout.minimumHeight: 40
                Layout.maximumHeight: 40
            }

            Image {
                    Layout.alignment: Qt.AlignHCenter
                    fillMode: Image.Pad
                    source: showQR.addressItem ? viewModel.generateQR(showQR.addressItem.address, 164, 164) : ""
            }
            Item {
                Layout.fillHeight: true
                Layout.minimumHeight: 40
                Layout.maximumHeight: 40
            }

            SFText {
                Layout.alignment: Qt.AlignHCenter
                //: show qr dialog address label
                //% "Your address"
                text: qsTrId("show-qr-tx-token-label") + ":"
                color: Style.content_main
                font.pixelSize: 14
                font.styleName: "Bold"; font.weight: Font.Bold
            }

            Item {
                Layout.fillHeight: true
                Layout.minimumHeight: 10
                Layout.maximumHeight: 10
            }

            Item {
                Layout.fillHeight: true
                Layout.minimumHeight: 45
                Layout.maximumHeight: 45
            
                SFLabel {
                    height: 48
                    width: 392
                    horizontalAlignment: Text.AlignHCenter
                    text: showQR.addressItem ? showQR.addressItem.address : ""
                    color: Style.content_secondary
                    font.pixelSize: 14
                    wrapMode: Text.Wrap
                    copyMenuEnabled: true
                    onCopyText: BeamGlobals.copyToClipboard(text)
                }
            }

            Item {
                Layout.fillHeight: true
                Layout.minimumHeight: 20
                Layout.maximumHeight: 20
            }

            SFText {
                // width: 400
                Layout.preferredWidth: 400
                Layout.minimumHeight: 32
                Layout.maximumHeight: 48
                horizontalAlignment: Text.AlignHCenter
                //: show QR dialog message, how to use this QR
                //% "Scan this QR code or send this address to the sender over secure channel"
                text: qsTrId("show-qr-message")
                color: Style.content_main
                wrapMode: Text.WordWrap
                font.pixelSize: 14
            }

            Item {
                Layout.fillHeight: true
                Layout.minimumHeight: 25
                Layout.maximumHeight: 25
            }

            Row {
                Layout.alignment: Qt.AlignHCenter
                CustomButton {
                    height: 38
                    //% "Close"
                    text: qsTrId("general-close")
                    Layout.alignment: Qt.AlignHCenter
                    icon.source: "qrc:/assets/icon-cancel-16.svg"
                    onClicked: showQR.close()
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.minimumHeight: 40
        Layout.maximumHeight: 40
        Layout.topMargin: 54

        TxFilter{
            id: activeAddressesFilter
            //% "My active addresses"
            label: qsTrId("addresses-tab-active")
            onClicked: addressRoot.state = "active"
            capitalization: Font.AllUppercase
        }

        TxFilter{
            id: expiredAddressesFilter
            //% "My expired addresses"
            label: qsTrId("addresses-tab-expired")
            onClicked: addressRoot.state = "expired"
            capitalization: Font.AllUppercase
        }

        TxFilter{
            id: contactsFilter
            //% "Contacts"
            label: qsTrId("addresses-tab-contacts")
            onClicked: addressRoot.state = "contacts"
            capitalization: Font.AllUppercase
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
            parentModel: viewModel
            visible: false

            editDialog: editActiveAddress
            showQRDialog: showQR

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
            parentModel: viewModel

            editDialog: editExpiredAddress
            showQRDialog: showQR
            isExpired: true

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

            property int rowHeight: 56
            property int resizableWidth: parent.width - actions.width
            property double columnResizeRatio: resizableWidth / 740

            anchors.fill: parent
            frameVisible: false
            selectionMode: SelectionMode.NoSelection
            backgroundVisible: false
            model: viewModel.contacts
            sortIndicatorVisible: true
            sortIndicatorColumn: 0
            sortIndicatorOrder: Qt.DescendingOrder
            
            Binding{
                target: viewModel
                property: "contactSortRole"
                value: contactsView.getColumn(contactsView.sortIndicatorColumn).role
            }

            Binding{
                target: viewModel
                property: "contactSortOrder"
                value: contactsView.sortIndicatorOrder
            }

            TableViewColumn {
                role: viewModel.nameRole
                //% "Comment"
                title: qsTrId("general-comment")
                width: 280 * contactsView.columnResizeRatio
                movable: false
            }

            TableViewColumn {
                role: viewModel.addressRole
                //% "Contact"
                title: qsTrId("general-contact")
                width: 170 * contactsView.columnResizeRatio
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
                            color: Style.content_main
                            copyMenuEnabled: true
                            onCopyText: BeamGlobals.copyToClipboard(text)
                        }
                    }
                }
            }

            TableViewColumn {
                id: categoryColumn
                role: viewModel.categoryRole
                //% "Category"
                title: qsTrId("general-category")
                width: contactsView.getAdjustedColumnWidth(categoryColumn)//290 * contactsView.columnResizeRatio
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
                    color: styleData.selected ? Style.row_selected :
                            (styleData.alternate ? Style.background_row_even : Style.background_row_odd)
                }
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.RightButton
                    onClicked: {
                        if (mouse.button == Qt.RightButton && styleData.row != undefined)
                        {
                            contextMenu.address = contactsView.model[styleData.row].address;
                            contextMenu.popup();
                        }
                    }
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
                                //% "Actions"
                                ToolTip.text: qsTrId("general-actions")
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
                Action {
                    //% "Delete contact"
                    text: qsTrId("address-table-cm-delete-contact")
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
