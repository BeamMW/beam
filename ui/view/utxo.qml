import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Styles 1.2
import "controls"
import Beam.Wallet 1.0;

ColumnLayout {
    anchors.fill: parent
    UtxoViewModel {id: viewModel}

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
            color: Style.white
            text: qsTr("UTXO")
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

                SFText {
                    Layout.minimumHeight: 20
                    Layout.maximumHeight: 20
                    font.pixelSize: 18
                    font.styleName: "Bold"; font.weight: Font.Bold
                    color: Style.white
                    text: qsTr("Height")
                }

                SFText {
                    Layout.minimumHeight: 20
                    Layout.maximumHeight: 20
                    font.pixelSize: 16
                    color: Style.bright_teal
                    text: viewModel.currentHeight
                }
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

                SFText {
                    Layout.fillWidth: true
                    Layout.minimumHeight: 20
                    Layout.maximumHeight: 20
                    font.pixelSize: 18
                    font.styleName: "Bold"; font.weight: Font.Bold
                    color: Style.white
                    text: qsTr("Hash")
                }

                SFText {
                    Layout.fillWidth: true
                    Layout.minimumHeight: 20
                    Layout.maximumHeight: 20
                    font.pixelSize: 16
                    color: Style.bright_teal
                    text: viewModel.currentStateHash
                    elide: Text.ElideRight
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

    CustomTableView {
        id: tableView
        property int rowHeight: 69
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.bottomMargin: 9
        frameVisible: false
        selectionMode: SelectionMode.NoSelection
        backgroundVisible: false
        model: viewModel.allUtxos
        sortIndicatorVisible: true
        sortIndicatorColumn: 1
        sortIndicatorOrder: Qt.DescendingOrder

        Binding{
            target: viewModel
            property: "sortRole"
            value: tableView.getColumn(tableView.sortIndicatorColumn).role
        }

        Binding{
            target: viewModel
            property: "sortOrder"
            value: tableView.sortIndicatorOrder
        }

        TableViewColumn {
            role: viewModel.amountRole
            title: qsTr("Amount")
            width: 150 * parent.width / 800
            movable: false
        }

        TableViewColumn {
            role: viewModel.heightRole
            title: qsTr("Height")
            width: 150 * parent.width / 800
            movable: false
        }

        TableViewColumn {
            role: viewModel.maturityRole
            title: qsTr("Maturity")
            width: 150 * parent.width / 800
            movable: false
        }

        TableViewColumn {
            role: viewModel.statusRole
            title: qsTr("Status")
            width: 200 * parent.width / 800
            movable: false
            resizable: false
            delegate: Item {
                id: delegate_id
                width: parent.width
                height: tableView.rowHeight
                readonly property var lineSeparator: "\n"
                property var texts: styleData.value.split(lineSeparator)
                property color secondLineColor: "#879daa"

                ColumnLayout {
                    anchors.right: parent.right
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter

                    SFLabel {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.topMargin: secondLineEnabled() ? 0 : 20
                        color: getTextColor(delegate_id.texts)
                        elide: Text.ElideRight
                        text: delegate_id.texts[0]
                        textFormat: Text.StyledText
                        font.italic: true
                        font.pixelSize: 14
                    }

                    SFLabel {
                        Layout.fillWidth: true
                        Layout.fillHeight: secondLineEnabled()
                        enabled: secondLineEnabled()
                        color: delegate_id.secondLineColor
                        elide: Text.ElideRight
                        text: secondLineEnabled() ? delegate_id.texts[1] : ""
                        textFormat: Text.StyledText
                        font.italic: true
                        font.pixelSize: 14
                    }
                }

                function getTextColor(texts) {
                    if (texts[0] === "available") {
                        return Style.bright_teal;
                    }
                    else if (texts[0] == "maturing") {
                        return Style.heliotrope;
                    }
                    else if (texts[0] === "spent") {
                        return Style.heliotrope;
                    }
                    else if (texts[0] === "unavailable") {
                        return Style.white;
                    }
                    else if (texts[0] === "in progress") {
                        if (texts[1] === "(change)" || texts[1] === "(outgoing)") {
                            return Style.heliotrope;
                        }
                        else {
                            return Style.bright_sky_blue;
                        }
                    }
                    else {
                        console.log("Unknown status!");
                        return Style.white;
                    }
                }

                function secondLineEnabled() {
                    return delegate_id.texts[1] !== undefined;
                }
            }
        }


        TableViewColumn {
            role: viewModel.typeRole
            title: qsTr("Type")
            width: 150 * parent.width / 800
            movable: false
        }

        rowDelegate: Item {

            height: tableView.rowHeight

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
            elide: Text.ElideRight
        }
    }
}
