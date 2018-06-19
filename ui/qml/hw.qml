import QtQuick 2.2
import QtQuick.Controls 1.0

Rectangle {
    id: rectangle

    width: 800
    height: 600

    Text {
        id: text2
        text: qsTr("Transactions")
        font.bold: true
        anchors.top: parent.top
        anchors.topMargin: 50
        anchors.left: parent.left
        anchors.leftMargin: 0
        font.pixelSize: 16
    }

    Column {
        id: column
        spacing: 0
        anchors.fill: parent

        Text {
            id: text1
            text: model.label
            horizontalAlignment: Text.AlignLeft
            font.pixelSize: 12
        }

        Button {
            id: button
            text: qsTr("Button")
            onClicked: model.sayHello("Beam")
        }
    }

    ListModel {
        id: sourceModel

        ListElement
        {
            date:       "12 June 2018 | 3:46 PM"
            user:       "super_user"
            comment:    "Beam is super cool, bla bla bla..."
            amount:     "+0.63736 BEAM"
            amountUsd:  "726.4 USD"
            status:     "unspent"
        }

        ListElement
        {
            date:       "12 June 2018 | 3:46 PM"
            user:       "super_user"
            comment:    "Beam is super cool, bla bla bla..."
            amount:     "+0.63736 BEAM"
            amountUsd:  "726.4 USD"
            status:     "unspent"
        }

        ListElement
        {
            date:       "12 June 2018 | 3:46 PM"
            user:       "super_user"
            comment:    "Beam is super cool, bla bla bla..."
            amount:     "+0.63736 BEAM"
            amountUsd:  "726.4 USD"
            status:     "unspent"
        }

        ListElement
        {
            date:       "12 June 2018 | 3:46 PM"
            user:       "super_user"
            comment:    "Beam is super cool, bla bla bla..."
            amount:     "+0.63736 BEAM"
            amountUsd:  "726.4 USD"
            status:     "unspent"
        }

        ListElement
        {
            date:       "12 June 2018 | 3:46 PM"
            user:       "super_user"
            comment:    "Beam is super cool, bla bla bla..."
            amount:     "+0.63736 BEAM"
            amountUsd:  "726.4 USD"
            status:     "unspent"
        }

        ListElement
        {
            date:       "12 June 2018 | 3:46 PM"
            user:       "super_user"
            comment:    "Beam is super cool, bla bla bla..."
            amount:     "+0.63736 BEAM"
            amountUsd:  "726.4 USD"
            status:     "unspent"
        }

    }

    TableView {
        id: tableView
        anchors.topMargin: 75
        anchors.fill: parent

        frameVisible: false
        sortIndicatorVisible: true


        TableViewColumn {
            id: dateColumn
            title: "Date | time"
            role: "date"
            movable: false
        }

        TableViewColumn {
            id: userColumn
            title: "User ID"
            role: "user"
            movable: false
        }

        TableViewColumn {
            id: commentColumn
            title: "Comment"
            role: "comment"
            movable: false
        }

        TableViewColumn {
            id: amountColumn
            title: "Amount, BEAM"
            role: "amount"
            movable: false
        }

        TableViewColumn {
            id: amountUsdColumn
            title: "Amount, USD"
            role: "amountUsd"
            movable: false
        }

        TableViewColumn {
            id: amountstatusColumn
            title: "Status"
            role: "status"
            movable: false
        }

        model: sourceModel

    }

}
