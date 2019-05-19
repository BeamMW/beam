import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls.impl 2.4
import QtQuick.Layouts 1.0
import QtQuick.Controls.Styles 1.2
import "."

TableView {
    id: tableView
    property int headerHeight: 46
    property int headerTextFontSize: 14
    property int headerTextLeftMargin: 20

    style: TableViewStyle {
        transientScrollBars: true
        minimumHandleLength: 20
        handle: Rectangle {
            implicitWidth: 14
            implicitHeight: 16
            radius: 6
            anchors.fill: parent
            color: Style.white  //
            opacity: 0.1
        }
    }
    horizontalScrollBarPolicy: Qt.ScrollBarAlwaysOff

    headerDelegate: Rectangle {
        height: headerHeight

        color: Style.background_second

        IconLabel {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: headerTextLeftMargin
            font.pixelSize: headerTextFontSize
            color: tableView.sortIndicatorColumn == styleData.column ? Style.content_main : Style.content_secondary
            font.weight: tableView.sortIndicatorColumn == styleData.column ? Font.Bold : Font.Normal
            font.family: "SF Pro Display"
            font.styleName: "Regular"

            icon.source: styleData.value == "" ? "" : tableView.sortIndicatorColumn == styleData.column ? "qrc:/assets/icon-sort-active.svg" : "qrc:/assets/icon-sort.svg"
            icon.width: 5
            icon.height: 8
            spacing: 6
            mirrored: true

            text: styleData.value
        }
    }
}