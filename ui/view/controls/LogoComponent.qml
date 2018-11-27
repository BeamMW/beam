import QtQuick 2.11
import QtQuick.Controls 2.4
import "."

Component
{
    //id: logoComponent

    Column
    {
        spacing: 20

        Image {
            anchors.horizontalCenter: parent.horizontalCenter

            source: "qrc:/assets/start-logo.svg"
            width: 242
            height: 170
        }

        SFText {
            anchors.horizontalCenter: parent.horizontalCenter

            text: qsTr("BEAM")
            color: "#25c1ff"
            font.pixelSize: 32
            font.styleName: "Bold"; font.weight: Font.Bold
            font.letterSpacing: 20
        }

        SFText {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.topMargin: 20

            text: qsTr("Scalable confidential cryptocurrency")

            color: "#25c1ff"
            font.pixelSize: 18
            font.styleName: "Bold"; font.weight: Font.Bold
        }
    }
}