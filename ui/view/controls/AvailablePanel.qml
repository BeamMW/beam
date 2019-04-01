import QtQuick 2.11
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.0
import "."

Rectangle {
    id: panel
    radius: 10
    color: Style.background_second

    clip: true

    property string value
    property alias color: panel.color

    signal copyValueText()

    SFText {
        id: title
        font {
            pixelSize: 18
            styleName: "Bold"; weight: Font.Bold
        }

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: 30
        anchors.leftMargin: 30
        
        color: Style.content_main
        text: qsTr("Available")
    }

    Row
    {
        id: amount
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: title.left

        spacing: 6

        SFLabel {
            id: amount_text
            font.pixelSize: 36
            font.styleName: "Light"; font.weight: Font.Light
            color: Style.active

            text: value
            anchors.bottom: parent.bottom
            copyMenuEnabled: true
            onCopyText: panel.copyValueText()
        }

        SFText {
            id: currency_text
            font.pixelSize: 24
            font.styleName: "Light"; font.weight: Font.Light
            color: Style.active

            text: "BEAM"
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 3
        }
    }
}
