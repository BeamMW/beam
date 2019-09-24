import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls.impl 2.4
import QtQuick.Layouts 1.0
import QtQuick.Controls.Styles 1.2
import QtGraphicalEffects 1.0
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

    function getAdjustedColumnWidth(column) {
        var acc = 0;
        for (var i = 0; i < columnCount; ++i)
        {
            var c = getColumn(i);
            if (c == column) continue;
            acc += c.width;
        }
        return width - acc;
    }

    frameVisible: false
    backgroundVisible: false
    horizontalScrollBarPolicy: Qt.ScrollBarAlwaysOff

    headerDelegate: Rectangle {
        id: rect
        height: headerHeight
       
        color:"transparent"// Style.background_main

        ShaderEffectSource {
            id: shaderSrc
            objectName: "renderRect"

            sourceRect.x: rect.mapToItem(main.backgroundRect, rect.x, rect.y).x
            sourceRect.y: rect.mapToItem(main.backgroundRect, rect.x, rect.y).y
            sourceRect.width: rect.width
            sourceRect.height: rect.height
            width: rect.width
            height: rect.height
            sourceItem: main.backgroundRect//backRect
            visible: true
        }

        property bool lastColumn: styleData.column == tableView.columnCount-1
        property bool firstOrLastColumn : styleData.column == 0 || lastColumn
        
        clip: firstOrLastColumn
        Rectangle {
            x: lastColumn ? -12 : 0
            width: parent.width + (firstOrLastColumn ? 12 : 0)
            height: parent.height + (firstOrLastColumn ? 12 : 0)
            color: Style.table_header
            radius: firstOrLastColumn ? 10 : 0
        }

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