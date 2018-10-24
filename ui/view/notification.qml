import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2

Rectangle {

    anchors.fill: parent
    color: "#032e48"

    Text {
        anchors.centerIn: parent
        font.pixelSize: 40
        color: "#ffffff"
        text: qsTr("Notifications view")
    }
}
