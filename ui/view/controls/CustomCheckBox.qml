import QtQuick 2.11
import QtQuick.Controls 1.4
import QtQuick.Controls.Styles 1.4
import QtQuick.Controls.impl 2.4

CheckBox {
    id: control
    property color mainColor: control.checkedState == Qt.Checked ? Style.active : Style.content_main

    style: CheckBoxStyle {
        indicator: Rectangle {
            implicitWidth:  16
            implicitHeight: 16
            border.color:   Qt.rgba(mainColor.r, mainColor.g, mainColor.b, 0.5)
            border.width:   1
            radius:         1
            color:          control.checkedState == Qt.Checked ? Qt.rgba(mainColor.r, mainColor.g, mainColor.b, 0.5) : "transparent"
            ColorImage {
                visible: control.checkedState == Qt.Checked
                source:  "qrc:/qt-project.org/imports/QtQuick/Controls.2/images/check.png"
                color:   "transparent"
                anchors.fill: parent
            }
        }
        label: Text {
            text:  control.text
            color: Qt.rgba(mainColor.r, mainColor.g, mainColor.b, 0.5)
            leftPadding: 5
            font {
                family:     "SF Pro Display"
                styleName:  "Regular"
                weight:     Font.Normal
                pixelSize:  14
            }
        }
    }
}
