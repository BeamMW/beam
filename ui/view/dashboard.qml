import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2

Rectangle {

    anchors.fill: parent
    color: Style.marine

    Text {
        anchors.centerIn: parent
        font.pixelSize: 40
        color: Style.white
        text: "Welcome to Beam Testnet"
    }
}
