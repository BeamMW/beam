import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Styles 1.2
import "controls"
import Beam.Wallet 1.0;

ColumnLayout {
    anchors.fill: parent
    SwapOffersViewModel {id: viewModel}

    RowLayout {
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        Layout.bottomMargin: 10

        height: 80
        spacing: 10

        SFText {
            Layout.alignment: Qt.AlignTop
            Layout.minimumHeight: 40
            Layout.maximumHeight: 40
            font.pixelSize: 36
            color: Style.content_main
            //% "Offers"
            text: qsTrId("offers-title")
        }

        Item {
            Layout.fillWidth: true
        }

        Item {
            Layout.fillWidth: true
            height: parent.height

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.topMargin: 10
                anchors.bottomMargin: 20
                spacing: 5

                RowLayout {
                    Layout.fillWidth: true

                    SFTextInput {
                        id: sendAmountInput
                        property double amount: 0
                        Layout.fillWidth: true
                        //% "Amount..."
                        placeholderText: "Amount..."
                        font.pixelSize: 18
                        font.styleName: "Light"
                        font.weight: Font.Light
                        color: Style.accent_outgoing

                        validator: RegExpValidator { regExp: /^(([1-9][0-9]{0,7})|(1[0-9]{8})|(2[0-4][0-9]{7})|(25[0-3][0-9]{6})|(0))(\.[0-9]{0,7}[1-9])?$/ }

                        onTextChanged: {
                            if (focus) {
                                amount = text ? text : 0;
                            }
                        }

                        onFocusChanged: {
                            if (amount > 0) {
                                text = amount.toLocaleString(focus ? Qt.locale("C") : Qt.locale(), 'f', -128);
                            }
                        }
                    }
                    
                    SFTextInput {
                        id: sendMsgInput
                        Layout.fillWidth: true
                        //% "Message..."
                        placeholderText: "Message..."
                        font.pixelSize: 18
                        font.styleName: "Light"
                        font.weight: Font.Light
                        color: Style.accent_outgoing
                    }

                    CustomButton {
                        id: sendOfferButton
                        Layout.fillWidth: true
                        font.pixelSize: 18
                        font.styleName: "Bold"; font.weight: Font.Bold
                        text: "send offer"
                        onClicked: viewModel.sendSwapOffer(sendAmountInput.amount, sendMsgInput.text)
                    }
                }

                SFTextInput {
                    id: searchBox
                    Layout.fillWidth: true
                    Layout.minimumHeight: 20
                    Layout.maximumHeight: 80
                    font.pixelSize: 18
                    font.styleName: "Bold"; font.weight: Font.Bold
                    color: Style.content_main
                    //% "Search..."
                    placeholderText: "Search..."
                    // text: qsTrId("utxo-last-block-hash")
                    inputMethodHints: Qt.ImhNoPredictiveText
                }
            }

            Rectangle {
                anchors.fill: parent
                radius: 10
                color: Style.white
                opacity: 0.1
            }
        }
    }

    StatusBar {
        id: status_bar
        model: statusbarModel
    }

    CustomTableView {
        id: tableView
        property int rowHeight: 69
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.bottomMargin: 9
        frameVisible: false
        selectionMode: SelectionMode.NoSelection
        backgroundVisible: false
        sortIndicatorVisible: true
        sortIndicatorColumn: 1
        sortIndicatorOrder: Qt.DescendingOrder

        model: SortFilterProxyModel {
            id: proxyModel
            source: viewModel.allOffers

            sortOrder: tableView.sortIndicatorOrder
            sortCaseSensitivity: Qt.CaseInsensitive
            sortRole: tableView.getColumn(tableView.sortIndicatorColumn).role + "Sort"

            filterString: "*" + searchBox.text + "*"
            filterSyntax: SortFilterProxyModel.Wildcard
            filterCaseSensitivity: Qt.CaseInsensitive
        }

        TableViewColumn {
            role: "time"
            //% "Last updated"
            title: "Last updated"
            width: parent.width / 5
            movable: false
        }

        TableViewColumn {
            role: "id"
            //% "Id"
            title: "Id"
            width: parent.width / 5
            movable: false
        }

        TableViewColumn {
            role: "amount"
            //% "Amount"
            title: qsTrId("general-amount")
            width: parent.width / 5
            movable: false
        }

        TableViewColumn {
            role: "status"
            //% "Status"
            title: qsTrId("general-status")
            width: parent.width / 5
            movable: false
        }

        TableViewColumn {
            role: "message"
            //% "Message"
            title: "Message"
            width: parent.width / 5
            movable: false
        }
        
        rowDelegate: Item {
            height: tableView.rowHeight
            anchors.left: parent.left
            anchors.right: parent.right

            Rectangle {
                anchors.fill: parent
                color: Style.background_row_even
                visible: styleData.alternate
            }
        }

        itemDelegate: TableItem {
            text: styleData.value
            elide: Text.ElideRight
        }
    }
}
