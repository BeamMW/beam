import QtQuick 2.4
import QtQuick.Controls 2.4
import QtQuick.Controls.Styles 1.2
import QtQuick.Layouts 1.0
import "."

Rectangle {
	id: panel
    radius: 10
    color: Style.dark_slate_blue

    clip: true

    property string value
	property alias color: panel.color

    SFText {
        id: title
        font {
            pixelSize: 18
            weight: Font.Bold
        }

        color: Style.white

        x: 30
        y: 30
        text: qsTr("Available")
    }

    Row
    {
        x: 30
        y: 61
        spacing: 6

        SFText {
            font.pixelSize: 64
            font.weight: Font.ExtraLight
            color: Style.bright_teal

            text: value

            anchors.bottom: parent.bottom
        }

        SFText {
            font.pixelSize: 36
            font.weight: Font.ExtraLight
            color: Style.bright_teal

            text: "BEAM"
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
        }
    }
}
