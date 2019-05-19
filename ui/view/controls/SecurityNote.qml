import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Layouts 1.3
import QtGraphicalEffects 1.0

import "."

ColumnLayout {
    id: secutiryItem
    property alias iconSource: icon.source
    property alias text: label.text

    Item {
        Layout.fillWidth: true
        Layout.maximumHeight:76
        Layout.minimumHeight:76
        SvgImage {
            id: icon
            anchors.horizontalCenter: parent.horizontalCenter
        }
        DropShadow {
            anchors.fill: icon
            radius: 7
            samples: 9
            color: Style.active
            source: icon
        }
    }
    
    SFText {
        Layout.fillHeight: true
        Layout.minimumWidth: 200
        Layout.maximumWidth: 200
        id: label
        horizontalAlignment: Qt.AlignHCenter
        color: Style.content_main
        font.pixelSize: 14
        wrapMode: Text.WordWrap
    }
}
