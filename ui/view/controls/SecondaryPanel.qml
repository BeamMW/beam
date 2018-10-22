import QtQuick 2.11
import QtQuick.Controls 1.2
import QtQuick.Controls.Styles 1.2
import "."

Rectangle {
    property string title
    property string value
    property color amountColor

    radius: 10
    color: Style.dark_slate_blue

    clip: true

    SFText {
        id: title_id
        font {
            pixelSize: 18
            styleName: "Bold"; weight: Font.Bold
        }

        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: 30
        anchors.leftMargin: 30

        color: Style.white
        text: title
    }

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: title_id.left
        spacing: 6

        SFText {
            font {
                styleName: "Light"; weight: Font.ExtraLight
                pixelSize: 36
            }

            color: amountColor

            text: value

            anchors.bottom: parent.bottom
        }

        SFText {
            font {
                pixelSize: 24
                styleName: "Light"; weight: Font.ExtraLight
            }

            color: amountColor

            text: "BEAM"
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 3
        }
    }
}
