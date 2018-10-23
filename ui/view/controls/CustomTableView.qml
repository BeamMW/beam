import QtQuick 2.3
import QtQuick.Controls 1.2
import QtQuick.Layouts 1.0
import QtQuick.Controls.Styles 1.2
import "."

TableView {
    property bool roundedHeader: true
    property int headerTopRadius: 10
    property color backgroundColor: Style.marine
    property color headerColor: Style.dark_slate_blue

    property int headerHeight: 46
    property int headerTextFontSize: 14
    property int headerTextLeftMargin: 20

    style: TableViewStyle {
        transientScrollBars: true

        handle: Rectangle {
            anchors.topMargin: headerHeight
            implicitWidth: 14
            implicitHeight: 16
            radius: 6
            //anchors.fill: parent ???
            color: Style.white
            opacity: 0.1
        }
    }
    horizontalScrollBarPolicy: Qt.ScrollBarAlwaysOff

    headerDelegate: Rectangle {
        height: headerHeight

        color: Style.dark_slate_blue

        SFText {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: headerTextLeftMargin
            font.pixelSize: headerTextFontSize
            color: Style.bluey_grey

            text: styleData.value
        }
    }

    Rectangle {
        visible: parent.roundedHeader
        height: parent.headerTopRadius
        width: parent.headerTopRadius
        color: parent.backgroundColor
    }

    Rectangle {
        visible: parent.roundedHeader
        height: 2 * parent.headerTopRadius
        width: 2 * parent.headerTopRadius
        radius: 2 * parent.headerTopRadius
        color: parent.headerColor
    }

    Rectangle {
        visible: parent.roundedHeader
        anchors.right: parent.right
        height: parent.headerTopRadius
        width: parent.headerTopRadius
        color: parent.backgroundColor
    }

    Rectangle {
        visible: parent.roundedHeader
        anchors.right: parent.right
        height: 2 * parent.headerTopRadius
        width: 2 * parent.headerTopRadius
        radius: 2 * parent.headerTopRadius
        color: parent.headerColor
    }
}