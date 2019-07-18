import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Styles 1.2
import "controls"
import Beam.Wallet 1.0;

ColumnLayout {
    anchors.fill: parent
    // SwapOffersViewModel {id: viewModel}

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
            font.capitalization: Font.AllUppercase
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

            }

            Rectangle {
                anchors.fill: parent
                radius: 10
                color: Style.white
                opacity: 0.1
            }
        }

        Item {
            Layout.fillWidth: true
            height: parent.height

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 20
                anchors.rightMargin: 20
                anchors.topMargin: 10
                anchors.bottomMargin: 20
                spacing: 5

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

    TextField {
        id: searchBox
        Layout.fillWidth: true
        height: 80
        placeholderText: "Search..."
        inputMethodHints: Qt.ImhNoPredictiveText

        // width: window.width / 5 * 2
        // anchors.right: parent.right
        // anchors.verticalCenter: parent.verticalCenter
    }

    // ListModel {
    //     id: mockModel
    //     ListElement {
    //         amount: "1"
    //         status: "active"
    //     }
    //     ListElement {
    //         amount: "2"
    //         status: "blocked"
    //     }
    //     ListElement {
    //         amount: "1"
    //         status: "expired"
    //     }
    // }

    // ListView {
    //     id: swapsListView
    //     property int rowHeight: 69
    //     Layout.fillWidth: true
    //     Layout.fillHeight: true
    //     Layout.bottomMargin: 9
    //     // frameVisible: false
    //     // selectionMode: SelectionMode.NoSelection
    //     // backgroundVisible: false
    //     // sortIndicatorVisible: true
    //     // sortIndicatorColumn: 1
    //     // sortIndicatorOrder: Qt.DescendingOrder

    //     // model: viewModel.allSwapOffers
    //     model: mockModel
    //     delegate: Row {
    //         Text { text: 'amount' + amount;}
    //         Text { text: 'status' + status;}
    //     }

    //     // model: SortFilterProxyModel {
    //     //     id: proxyModel
    //     //     source: viewModel.allSwapOffers

    //     //     sortOrder: tableView.sortIndicatorOrder
    //     //     sortCaseSensitivity: Qt.CaseInsensitive
    //     //     sortRole: tableView.getColumn(tableView.sortIndicatorColumn).role + "Sort"

    //     //     filterString: "*" + searchBox.text + "*"
    //     //     filterSyntax: SortFilterProxyModel.Wildcard
    //     //     filterCaseSensitivity: Qt.CaseInsensitive
    //     // }
    // }
}
