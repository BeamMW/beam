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

                    // width: window.width / 5 * 2
                    // anchors.right: parent.right
                    // anchors.verticalCenter: parent.verticalCenter
                }

                CustomButton {
                    id: sendTestButton

                    Layout.fillWidth: true
                    Layout.minimumHeight: 20
                    Layout.maximumHeight: 80
                    font.pixelSize: 18
                    font.styleName: "Bold"; font.weight: Font.Bold
                    // color: Style.content_main

                    text: "SendTest"
                    onClicked: viewModel.sendTestOffer()
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
            role: "amount"
            //% "Amount"
            title: qsTrId("general-amount")
            width: 300 * parent.width / 800
            movable: false
        }

        TableViewColumn {
            role: "status"
            //% "Status"
            title: qsTrId("general-status")
            width: 200 * parent.width / 800
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
