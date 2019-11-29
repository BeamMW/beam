import QtQuick 2.11
import QtQuick.Controls 1.4
import QtQuick.Controls.Styles 1.4
import QtQuick.Controls.impl 2.4

CheckBox {
    id: control
    property color mainColor: control.checkedState == Qt.Checked ? Style.active : Qt.rgba(Style.content_main.r, Style.content_main.g, Style.content_main.b, 0.5)

    style: CheckBoxStyle {
        indicator: Rectangle {
            implicitWidth:  15
            implicitHeight: 15
            border.color:   mainColor
            border.width:   1
            radius:         1
            color:          control.checkedState == Qt.Checked ? mainColor : "transparent"
            ColorImage {
                visible: control.checkedState == Qt.Checked
                source:  "qrc:/qt-project.org/imports/QtQuick/Controls.2/images/check.png"
                color:   "transparent"
                anchors.fill: parent
            }
        }
        label: Text {
            text:  control.text
            color: mainColor
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
